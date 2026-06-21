#!/usr/bin/env python3
"""Keep the companion app version in sync across package metadata."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGE_JSON = ROOT / "companion" / "package.json"
PACKAGE_LOCK = ROOT / "companion" / "package-lock.json"
TAURI_CONF = ROOT / "companion" / "src-tauri" / "tauri.conf.json"
CARGO_TOML = ROOT / "companion" / "src-tauri" / "Cargo.toml"
CARGO_LOCK = ROOT / "companion" / "src-tauri" / "Cargo.lock"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def load_json(path: Path) -> dict:
    with path.open() as f:
        return json.load(f)


def write_json(path: Path, data: dict) -> None:
    with path.open("w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def read_tauri_version() -> str:
    return load_json(TAURI_CONF)["version"]


def validate_version(version: str) -> None:
    if not VERSION_RE.match(version):
        raise SystemExit(f"invalid semver patch version: {version}")


def bump_patch(version: str) -> str:
    major, minor, patch = (int(part) for part in version.split("."))
    return f"{major}.{minor}.{patch + 1}"


def replace_package_version(text: str, package_name: str, version: str) -> str:
    package_header = f'name = "{package_name}"'
    start = text.find(package_header)
    if start == -1:
        raise SystemExit(f"could not find Cargo package {package_name}")
    next_package = text.find("\n[[package]]", start + len(package_header))
    end = len(text) if next_package == -1 else next_package
    package_block = text[start:end]
    updated_block, count = re.subn(
        r'version = "[^"]+"',
        f'version = "{version}"',
        package_block,
        count=1,
    )
    if count != 1:
        raise SystemExit(f"could not update Cargo package {package_name}")
    return text[:start] + updated_block + text[end:]


def set_version(version: str) -> None:
    validate_version(version)

    package_json = load_json(PACKAGE_JSON)
    package_json["version"] = version
    write_json(PACKAGE_JSON, package_json)

    package_lock = load_json(PACKAGE_LOCK)
    package_lock["version"] = version
    package_lock["packages"][""]["version"] = version
    write_json(PACKAGE_LOCK, package_lock)

    tauri_conf = load_json(TAURI_CONF)
    tauri_conf["version"] = version
    write_json(TAURI_CONF, tauri_conf)

    cargo_toml = CARGO_TOML.read_text()
    cargo_toml, count = re.subn(
        r'(\[package\][\s\S]*?\nversion = )"[^"]+"',
        rf'\1"{version}"',
        cargo_toml,
        count=1,
    )
    if count != 1:
        raise SystemExit("could not update Cargo.toml package version")
    CARGO_TOML.write_text(cargo_toml)

    cargo_lock = replace_package_version(
        CARGO_LOCK.read_text(),
        "kiro-keyboard-companion",
        version,
    )
    CARGO_LOCK.write_text(cargo_lock)


def check_versions() -> str:
    versions = {
        "companion/package.json": load_json(PACKAGE_JSON)["version"],
        "companion/package-lock.json": load_json(PACKAGE_LOCK)["version"],
        "companion/package-lock.json packages root": load_json(PACKAGE_LOCK)["packages"][""]["version"],
        "companion/src-tauri/tauri.conf.json": read_tauri_version(),
    }

    cargo_toml = CARGO_TOML.read_text()
    cargo_match = re.search(r'\[package\][\s\S]*?\nversion = "([^"]+)"', cargo_toml)
    if not cargo_match:
        raise SystemExit("could not read Cargo.toml package version")
    versions["companion/src-tauri/Cargo.toml"] = cargo_match.group(1)

    cargo_lock = CARGO_LOCK.read_text()
    lock_match = re.search(
        r'name = "kiro-keyboard-companion"\nversion = "([^"]+)"',
        cargo_lock,
    )
    if not lock_match:
        raise SystemExit("could not read Cargo.lock package version")
    versions["companion/src-tauri/Cargo.lock"] = lock_match.group(1)

    unique_versions = set(versions.values())
    if len(unique_versions) != 1:
        details = "\n".join(f"{path}: {version}" for path, version in versions.items())
        raise SystemExit(f"app versions are out of sync:\n{details}")

    version = unique_versions.pop()
    validate_version(version)
    return version


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: app_version.py get | bump-patch | set <version>")

    command = sys.argv[1]
    if command == "get":
        print(check_versions())
    elif command == "bump-patch":
        set_version(bump_patch(check_versions()))
        print(check_versions())
    elif command == "set":
        if len(sys.argv) != 3:
            raise SystemExit("usage: app_version.py set <version>")
        set_version(sys.argv[2])
        print(check_versions())
    else:
        raise SystemExit(f"unknown command: {command}")


if __name__ == "__main__":
    main()
