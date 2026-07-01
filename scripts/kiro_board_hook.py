#!/usr/bin/env python3
"""
Translate Kiro CLI hook events into board agent-state JSONL.

Usage:
  python3 scripts/kiro_board_hook.py --agent-name planner --serial-port /dev/cu.usbmodem5B901608471  # for MacOS
  python3 scripts/kiro_board_hook.py --agent-name planner --serial-port COM5        # for Windows

Serial port resolution priority:
  1. --serial-port CLI argument (explicit override)
  2. KIRO_BOARD_PORT environment variable
  3. Auto-discovery via USB VID/PID (0x303A/0x1001)
  4. Fallback to stdout (prints JSONL payload to stdout)

Companion HTTP endpoint:
  The script first tries to POST to the companion app's local HTTP endpoint.
  If that succeeds, no serial write is needed. If it fails (companion offline),
  the script falls back to writing directly to the board's USB CDC serial port.

  Endpoint: http://127.0.0.1:47218/hook (configurable via --companion-url or
  KIRO_COMPANION_HOOK_URL env var)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from typing import Optional


def discover_serial_port() -> Optional[str]:
    """Auto-discover ki-board by USB VID/PID.

    Searches for a connected device matching:
      - VID: 0x303A (Espressif)
      - PID: 0x1001 (ESP32-S3 USB CDC)

    Returns the device path (e.g. COM5, /dev/cu.usbmodemXXX) or None.
    """
    try:
        from serial.tools.list_ports import comports
    except ImportError:
        print(
            "pyserial not installed, auto-discovery unavailable",
            file=sys.stderr,
        )
        return None

    matches = [
        port.device
        for port in comports()
        if port.vid == 0x303A and port.pid == 0x1001
    ]

    if not matches:
        return None

    if len(matches) > 1:
        # Prefer port with "ki-board" in product string if available
        for port in comports():
            if (port.vid == 0x303A and port.pid == 0x1001 and
                    port.product and "ki-board" in port.product.lower()):
                return port.device
        print(
            f"Warning: {len(matches)} ESP32-S3 CDC devices found, using {matches[0]}",
            file=sys.stderr,
        )

    return matches[0]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Kiro hook to board state bridge")
    parser.add_argument(
        "--agent-name",
        required=True,
        help="Custom agent name used by the board, for example planner",
    )
    parser.add_argument(
        "--serial-port",
        help="Serial port to write JSONL events to (e.g. COM5, /dev/cu.usbmodemXXX)",
    )
    parser.add_argument(
        "--companion-url",
        default=os.environ.get(
            "KIRO_COMPANION_HOOK_URL", "http://127.0.0.1:47218/hook"
        ),
        help="Local companion hook endpoint (default: http://127.0.0.1:47218/hook)",
    )
    return parser.parse_args()


def read_hook_event() -> dict:
    raw = sys.stdin.read().strip()
    if not raw:
        raise SystemExit("expected Kiro hook JSON on stdin")
    return json.loads(raw)


def map_state(event_name: str) -> Optional[str]:
    if event_name == "agentSpawn":
        return "idle"
    if event_name == "userPromptSubmit":
        return "running"
    if event_name == "stop":
        return "idle"
    return None


def build_payload(event: dict, agent_name: str) -> Optional[dict]:
    event_name = event.get("hook_event_name")
    if not isinstance(event_name, str):
        return None

    if event_name == "postToolUse":
        status = event.get("tool_result_status")
        if not (isinstance(status, str) and status.lower() == "error"):
            return None
        payload = {
            "type": "agent_state",
            "agent_name": agent_name,
            "state": "error",
        }
        session_id = event.get("session_id")
        if isinstance(session_id, str) and session_id:
            payload["session_id"] = session_id
        cwd = event.get("cwd")
        if isinstance(cwd, str) and cwd:
            payload["cwd"] = cwd
        return payload

    state = map_state(event_name)
    if state is None:
        return None

    payload = {
        "type": "agent_state",
        "agent_name": agent_name,
        "state": state,
    }

    session_id = event.get("session_id")
    if isinstance(session_id, str) and session_id:
        payload["session_id"] = session_id

    cwd = event.get("cwd")
    if isinstance(cwd, str) and cwd:
        payload["cwd"] = cwd

    return payload


def emit_to_companion(line: str, companion_url: str) -> bool:
    """Try to POST the JSONL payload to the companion app's HTTP endpoint.
    Returns True if delivery succeeded, False otherwise.
    """
    if not companion_url:
        return False
    request = urllib.request.Request(
        companion_url,
        data=line.encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=0.4) as response:
            return 200 <= response.status < 300
    except (urllib.error.URLError, TimeoutError, OSError):
        return False


def emit_line(line: str, serial_port: Optional[str]) -> None:
    """Write JSONL line to serial port or stdout."""
    if serial_port:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise SystemExit(
                "pyserial is required when --serial-port is used: pip install pyserial"
            ) from exc

        import time

        max_attempts = 5

        # Platform-specific locking
        if sys.platform == "win32":
            # Windows: retry with backoff (serial port is exclusively locked by OS)
            for attempt in range(max_attempts):
                try:
                    port = serial.Serial(
                        serial_port, 115200, timeout=1, dsrdtr=False, rtscts=False
                    )
                    port.dtr = False
                    port.rts = False
                    port.write(line.encode("utf-8") + b"\n")
                    port.flush()
                    port.close()
                    return
                except (OSError, serial.SerialException):
                    if attempt < max_attempts - 1:
                        time.sleep(0.2 * (attempt + 1))
            return
        else:
            # Unix: use file lock to prevent concurrent writes
            import fcntl

            lock_path = f"/tmp/kiro_board_{os.path.basename(serial_port)}.lock"

            for attempt in range(max_attempts):
                lock_fd = None
                try:
                    lock_fd = open(lock_path, "w")
                    fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    port = serial.Serial(
                        serial_port, 115200, timeout=1, dsrdtr=False, rtscts=False
                    )
                    port.dtr = False
                    port.rts = False
                    port.write(line.encode("utf-8") + b"\n")
                    port.flush()
                    port.close()
                    return
                except (OSError, serial.SerialException):
                    if attempt < max_attempts - 1:
                        time.sleep(0.2 * (attempt + 1))
                finally:
                    if lock_fd:
                        fcntl.flock(lock_fd, fcntl.LOCK_UN)
                        lock_fd.close()
            return

    print(line)


def main() -> int:
    args = parse_args()
    event = read_hook_event()
    payload = build_payload(event, args.agent_name)
    if payload is None:
        return 0

    line = json.dumps(payload, separators=(",", ":"))

    # Priority 1: Try companion HTTP endpoint
    if emit_to_companion(line, args.companion_url):
        return 0

    # Priority 2: Resolve serial port (CLI arg > env var > auto-discovery)
    serial_port = args.serial_port
    if not serial_port:
        serial_port = os.environ.get("KIRO_BOARD_PORT") or None
    if not serial_port:
        serial_port = discover_serial_port()
        if serial_port:
            print(
                f"Auto-discovered ki-board at {serial_port}",
                file=sys.stderr,
            )

    # Priority 3: Write to serial port or stdout
    emit_line(line, serial_port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
