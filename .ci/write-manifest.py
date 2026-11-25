#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record the exact build inputs and image identity."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from hardware_targets import load_targets


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def git(repo, *args):
    return command("git", "-C", repo, *args)


def sdk_version(repo):
    text = Path(repo, "include/rtcp/version.h").read_text()
    parts = [re.search(rf"^#define RTCP_VERSION_{part}\s+(\d+)", text, re.M)[1]
             for part in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts) + re.search(r'^#define RTCP_VERSION_SUFFIX\s+"([^"]*)"', text, re.M)[1]


name, destination, config = sys.argv[1:]
dest = Path(destination)
target = load_targets()[name]
demo = "application/rt-thread/rt-carplay-demo"
sdk = "packages/third-party/rt-carplay"
[image] = dest.glob("*.img")
manifest = {
    "schema": 1,
    "hardware_target": name,
    "target": target,
    "firmware_version": image.read_bytes()[136:200].split(b"\0", 1)[0].decode("ascii"),
    "integration_commit": git(".", "rev-parse", "HEAD"),
    "luban_lite_version": "1.3.3",
    "luban_lite_baseline": "90a740fe9c1b663ea52ec3f7e1af35acde200aea",
    "demo_version": git(demo, "describe", "--tags", "--always"),
    "demo_commit": git(demo, "rev-parse", "HEAD"),
    "rt_carplay_version": sdk_version(sdk),
    "rt_carplay_commit": git(sdk, "rev-parse", "HEAD"),
    "submodules": git(".", "submodule", "status", "--recursive").splitlines(),
    "partition_layout_sha256": hashlib.sha256(Path(target["partition_layout"]).read_bytes()).hexdigest(),
    "toolchain": command(target["toolchain_prefix"] + "gcc", "--version").splitlines()[0],
    "scons": command("scons", "--version"),
    "source_worktree_changes": git(".", "status", "--porcelain", "--untracked-files=normal").splitlines(),
    "build_validation": "PASS",
    "hardware_validation": "NOT_RUN",
}
(dest / "source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
(dest / "firmware.config").write_bytes(Path(config).read_bytes())
