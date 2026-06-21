#!/usr/bin/env python3
"""Manage the firmware version stored in version.json."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "version.json"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def read_version_parts() -> tuple[int, int, int]:
    with VERSION_FILE.open() as f:
        version = json.load(f)
    return int(version["major"]), int(version["minor"]), int(version["patch"])


def write_version(version: str) -> None:
    if not VERSION_RE.match(version):
        raise SystemExit(f"invalid semver patch version: {version}")
    major, minor, patch = (int(part) for part in version.split("."))
    with VERSION_FILE.open("w") as f:
        json.dump({"major": major, "minor": minor, "patch": patch}, f, indent=2)
        f.write("\n")


def get_version() -> str:
    return ".".join(str(part) for part in read_version_parts())


def bump_patch() -> str:
    major, minor, patch = read_version_parts()
    version = f"{major}.{minor}.{patch + 1}"
    write_version(version)
    return version


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: firmware_version.py get | bump-patch | set <version>")

    command = sys.argv[1]
    if command == "get":
        print(get_version())
    elif command == "bump-patch":
        print(bump_patch())
    elif command == "set":
        if len(sys.argv) != 3:
            raise SystemExit("usage: firmware_version.py set <version>")
        write_version(sys.argv[2])
        print(get_version())
    else:
        raise SystemExit(f"unknown command: {command}")


if __name__ == "__main__":
    main()
