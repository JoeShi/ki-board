#!/usr/bin/env python3
"""
Translate Kiro CLI hook events into board agent-state JSONL.

Usage:
  python3 scripts/kiro_board_hook.py --agent-name planner --serial-port /dev/cu.usbmodem5B901608471

When --serial-port is omitted, the script prints the JSONL payload to stdout.
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Optional


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Kiro hook to board state bridge")
    parser.add_argument(
        "--agent-name",
        required=True,
        help="Custom agent name used by the board, for example planner",
    )
    parser.add_argument(
        "--serial-port",
        help="Optional serial port to write JSONL events to (for example /dev/cu.usbmodem5B901608471)",
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


def emit_line(line: str, serial_port: Optional[str]) -> None:
    if serial_port:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise SystemExit(
                "pyserial is required when --serial-port is used: pip install pyserial"
            ) from exc

        import fcntl, time, os

        lock_path = f"/tmp/kiro_board_{os.path.basename(serial_port)}.lock"
        max_attempts = 5

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

    emit_line(json.dumps(payload, separators=(",", ":")), args.serial_port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
