"""
PlatformIO pre-build script: auto-increment patch version on local builds.

Reads version.json, increments patch ONCE per pio run invocation (even if
multiple envs share this script), writes back, and injects
FW_VERSION_MAJOR / FW_VERSION_MINOR / FW_VERSION_PATCH as build flags.

In GitHub Actions release builds, version.json is bumped and committed by the
release workflow before PlatformIO runs, so this script only reads it there.
"""

import json
import os

Import("env")

VERSION_FILE = os.path.join(env.subst("$PROJECT_DIR"), "version.json")
LOCK_FILE = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "version.lock")

with open(VERSION_FILE, "r") as f:
    ver = json.load(f)

# Only increment if the parent pio process hasn't already done so.
# We write the parent PID into the lock; second env sees same PID -> skip.
ppid = str(os.getppid())
should_increment = os.environ.get("GITHUB_ACTIONS") != "true"
if os.path.exists(LOCK_FILE):
    with open(LOCK_FILE, "r") as f:
        if f.read().strip() == ppid:
            should_increment = False

if should_increment:
    ver["patch"] += 1
    with open(VERSION_FILE, "w") as f:
        json.dump(ver, f, indent=2)
        f.write("\n")
    os.makedirs(os.path.dirname(LOCK_FILE), exist_ok=True)
    with open(LOCK_FILE, "w") as f:
        f.write(ppid)

env.Append(CPPDEFINES=[
    ("FW_VERSION_MAJOR", ver["major"]),
    ("FW_VERSION_MINOR", ver["minor"]),
    ("FW_VERSION_PATCH", ver["patch"]),
])

print(f"  Firmware version: {ver['major']}.{ver['minor']}.{ver['patch']}")
