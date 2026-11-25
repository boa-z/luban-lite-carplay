#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Hardware target registry and build configuration checks."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_targets():
    targets = json.loads((ROOT / ".ci/hardware-targets.json").read_text(encoding="utf-8"))
    compatibility = set()
    aliases = 0
    for name, target in targets.items():
        if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", name):
            raise ValueError(f"Invalid hardware target ID: {name}")
        for path in (f"target/configs/{target['defconfig']}",
                     f"target/configs/{target['boot_defconfig']}", target["partition_layout"]):
            if not (ROOT / path).is_file():
                raise ValueError(f"{name}: missing {path}")
        ota = target.get("ota")
        if ota:
            compat = ota["compat"]
            if not re.fullmatch(r"[a-z0-9-]+", compat) or compat in compatibility:
                raise ValueError(f"{name}: invalid or duplicate OTA compatibility ID")
            compatibility.add(compat)
            aliases += bool(ota.get("legacy_alias"))
    if not targets or aliases > 1:
        raise ValueError("Expected targets and at most one legacy OTA alias")
    return targets


def check_config(target, path):
    actual = dict(re.findall(r"^CONFIG_(\w+)=(.*)$", Path(path).read_text(), re.M))
    required = {"PRJ_CHIP": json.dumps(target["platform"]),
                "PRJ_BOARD": json.dumps(target["board"]), **target["required_config"]}
    if target.get("ota"):
        required.update(RTCARPLAY_OTA="y", RTCARPLAY_OTA_COMPAT=json.dumps(target["ota"]["compat"]))
    for name, value in required.items():
        if actual.get(name, "n") != value:
            raise ValueError(f"CONFIG_{name}: expected {value}, got {actual.get(name, 'n')}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("matrix", "settings", "check"))
    parser.add_argument("target", nargs="?", default="all")
    parser.add_argument("--config", default=".config")
    options = parser.parse_args()
    targets = load_targets()
    if options.target not in targets and not (options.command == "matrix" and options.target == "all"):
        parser.error(f"unknown target: {options.target}")
    if options.command == "matrix":
        names = list(targets) if options.target == "all" else [options.target]
        print(json.dumps({"target": names}))
    elif options.command == "settings":
        target = targets[options.target]
        for key in ("defconfig", "boot_defconfig", "platform", "partition_layout", "toolchain_prefix"):
            print(target[key])
        print(",".join(target["checks"]))
        print(target.get("ota", {}).get("compat", ""))
    else:
        check_config(targets[options.target], options.config)


if __name__ == "__main__":
    main()
