#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Promote a complete, verified build without recompiling its payloads."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from urllib.parse import urlsplit

from hardware_targets import load_targets


def gh(*args):
    return subprocess.check_output(["gh", *map(str, args)], text=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_bundle(folder, target_id, target, commit, evidence):
    manifest = json.loads((folder / "source-manifest.json").read_text())
    if (manifest["hardware_target"] != target_id or manifest["integration_commit"] != commit
            or manifest["target"] != target or manifest["build_validation"] != "PASS"):
        raise ValueError(f"{target_id}: artifact source or target mismatch")
    [image] = folder.glob("*.img")
    members = {image.name, "bootcfg.txt", "source-manifest.json", "firmware.config"}
    if target.get("ota"):
        members.add("ota.cpio")
    checksums = {}
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        checksum, name = line.split("  ", 1)
        if name in checksums or name not in members or digest(folder / name) != checksum:
            raise ValueError(f"{target_id}: invalid checksum entry for {name}")
        checksums[name] = checksum
    if checksums.keys() != members or {path.name for path in folder.iterdir()} != members | {"SHA256SUMS"}:
        raise ValueError(f"{target_id}: missing or unexpected artifact files")
    url = evidence.get("url", "")
    parsed = urlsplit(url)
    if (evidence.get("status") != "PASS" or evidence.get("img_sha256") != checksums[image.name]
            or parsed.scheme != "https" or not parsed.hostname or re.search(r"[\s<>]", url)):
        raise ValueError(f"{target_id}: board evidence must cover this exact IMG SHA-256")
    return image, manifest["firmware_version"]


def prepare(run_id, evidence, staging):
    repo = os.environ["GH_REPO"]
    run = json.loads(gh("api", f"repos/{repo}/actions/runs/{run_id}"))
    if (run["path"] != ".github/workflows/firmware.yml" or run["conclusion"] != "success"
            or run["head_branch"] != "carplay" or run["head_repository"]["full_name"] != repo
            or run["event"] not in ("push", "workflow_dispatch")):
        raise ValueError("Expected a successful carplay firmware workflow run")
    names = load_targets()
    if evidence.keys() != names.keys():
        raise ValueError("Board evidence must cover every registered target")
    commit = run["head_sha"]
    artifacts = json.loads(gh("api", f"repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100"))
    available = {item["name"] for item in artifacts["artifacts"] if not item["expired"]}
    expected = {f"firmware-{name}-{commit}" for name in names}
    if not expected <= available:
        raise ValueError("Required target artifacts are missing or expired")
    assets, versions = [], set()
    notes = [f"Build run {run_id}, commit `{commit}`.", "", "Board validation"]
    for name, target in names.items():
        folder = staging / name
        gh("run", "download", run_id, "--name", f"firmware-{name}-{commit}", "--dir", folder)
        image, version = verify_bundle(folder, name, target, commit, evidence[name])
        versions.add(version)
        archive = staging / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(folder.iterdir()):
                bundle.write(path, arcname=path.name)
        assets.append(archive)
        if target.get("ota"):
            ota = staging / f"{name}.cpio"
            shutil.copyfile(folder / "ota.cpio", ota)
            assets.append(ota)
            if target["ota"].get("legacy_alias"):
                for path in (folder / "ota.cpio", image, folder / "bootcfg.txt"):
                    assets.append(path)
        notes.append(f"- `{name}` PASS `{evidence[name]['img_sha256']}` <{evidence[name]['url']}>")
    [version] = versions
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Expected one numeric firmware version across targets")
    report = staging / "hardware-validation.json"
    report.write_text(json.dumps({"run_id": run_id, "commit": commit, "targets": evidence}, indent=2) + "\n")
    assets.append(report)
    sums = staging / "SHA256SUMS"
    sums.write_text("".join(f"{digest(path)}  {path.name}\n" for path in assets))
    assets.append(sums)
    notes.append("\nEach target ZIP contains its matching IMG, bootcfg, config, manifest and checksums.")
    return commit, version, assets, "\n".join(notes)


def publish(channel, commit, version, assets, notes):
    tag = "nightly" if channel == "nightly" else f"firmware-v{version}"
    title = f"Firmware {channel} {version} ({commit[:8]})"
    repo = os.environ["GH_REPO"]
    if channel == "stable":
        ref = json.loads(gh("api", f"repos/{repo}/git/ref/tags/{tag}"))
        if ref["object"]["type"] != "commit" or ref["object"]["sha"] != commit:
            raise ValueError("Stable requires a lightweight firmware version tag at the validated commit")
        gh("release", "create", tag, *assets, "--verify-tag", "--title", title, "--notes", notes)
        return
    releases = json.loads(gh("release", "list", "--limit", "100", "--json", "tagName"))
    if not any(item["tagName"] == tag for item in releases):
        gh("release", "create", tag, *assets, "--target", commit, "--prerelease",
           "--latest=false", "--title", title, "--notes", notes)
        return
    gh("release", "edit", tag, "--draft=true", "--prerelease", "--title", title, "--notes", notes)
    gh("release", "upload", tag, *assets, "--clobber")
    current = json.loads(gh("release", "view", tag, "--json", "assets"))["assets"]
    expected = {path.name for path in assets}
    for asset in current:
        if asset["name"] not in expected:
            gh("release", "delete-asset", tag, asset["name"], "--yes")
    gh("api", "-X", "PATCH", f"repos/{repo}/git/refs/tags/{tag}", "-f", f"sha={commit}", "-F", "force=true")
    gh("release", "edit", tag, "--draft=false", "--prerelease", "--latest=false")


if __name__ == "__main__":
    run_id, channel, evidence = sys.argv[1:]
    if not run_id.isdecimal() or channel not in ("nightly", "stable"):
        sys.exit("Usage: promote-firmware.py RUN_ID nightly|stable EVIDENCE_JSON")
    with tempfile.TemporaryDirectory(prefix="firmware-promotion-") as folder:
        publish(channel, *prepare(run_id, json.loads(evidence), Path(folder)))
