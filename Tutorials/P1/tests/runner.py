#!/usr/bin/env python3
"""Simple command-protocol test runner for P1.

Usage:
  python3 tests/runner.py --mock tests/cases/smoke.json
  python3 tests/runner.py --port /dev/tty.usbmodem11201 tests/cases/smoke.json
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List


@dataclass
class Case:
    name: str
    send: str
    expect_prefix: str


class MockDevice:
    def __init__(self) -> None:
        self.led = 0
        self.pwm = 0

    def send(self, line: str) -> str:
        line = line.strip()
        if not line:
            return ""

        parts = line.split()
        cmd = parts[0]

        if cmd == "PING":
            return "OK PONG"
        if cmd == "HELP":
            return "OK CMDS=PING,HELP,VERSION,SET,GET (GET: LED,PWM,ADC,STAT)"
        if cmd == "VERSION":
            return "OK VERSION=P1-1.0"

        if cmd == "SET":
            if len(parts) < 3:
                return "ERR BAD_ARG"
            target, raw = parts[1], parts[2]
            try:
                value = int(raw)
            except ValueError:
                return "ERR BAD_ARG"

            if target == "LED":
                if value not in (0, 1):
                    return "ERR BAD_RANGE"
                self.led = value
                return f"OK LED={self.led}"
            if target == "PWM":
                if value < 0 or value > 255:
                    return "ERR BAD_RANGE"
                self.pwm = value
                return f"OK PWM={self.pwm}"
            return "ERR BAD_TARGET"

        if cmd == "GET":
            if len(parts) < 2:
                return "ERR BAD_ARG"
            target = parts[1]
            if target == "LED":
                return f"OK LED={self.led}"
            if target == "PWM":
                return f"OK PWM={self.pwm}"
            if target == "ADC":
                if len(parts) < 3:
                    return "ERR BAD_ARG"
                try:
                    ch = int(parts[2])
                except ValueError:
                    return "ERR BAD_ARG"
                if ch < 0 or ch > 5:
                    return "ERR BAD_RANGE"
                return "OK ADC=512"
            if target == "STAT":
                return "OK IRQ=42"
            return "ERR BAD_TARGET"

        return "ERR BAD_CMD"


class SerialDevice:
    def __init__(self, port: str, baud: int, timeout_s: float) -> None:
        try:
            import serial  # type: ignore
        except Exception as exc:  # pragma: no cover
            raise RuntimeError(
                "pyserial is required for --port mode. Install: pip install pyserial"
            ) from exc

        self._serial = serial.Serial(port=port, baudrate=baud, timeout=timeout_s)
        time.sleep(1.5)
        self._drain_boot()

    def _drain_boot(self) -> None:
        deadline = time.time() + 1.0
        while time.time() < deadline:
            if not self._serial.in_waiting:
                time.sleep(0.05)
                continue
            _ = self._serial.readline()

    def send(self, line: str) -> str:
        self._serial.write((line + "\n").encode("ascii"))
        raw = self._serial.readline().decode("ascii", errors="replace").strip()
        return raw

    def close(self) -> None:
        self._serial.close()


def load_cases(path: Path) -> List[Case]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    out: List[Case] = []
    for row in payload["cases"]:
        out.append(Case(name=row["name"], send=row["send"], expect_prefix=row["expect_prefix"]))
    return out


def run(cases: List[Case], device) -> int:
    passed = 0
    print(f"Running {len(cases)} test cases")
    for idx, case in enumerate(cases, 1):
        got = device.send(case.send)
        ok = got.startswith(case.expect_prefix)
        status = "PASS" if ok else "FAIL"
        print(f"[{idx:02d}] {status} {case.name}")
        print(f"     send: {case.send}")
        print(f"     got : {got}")
        print(f"     want: prefix '{case.expect_prefix}'")
        if ok:
            passed += 1

    print("-" * 50)
    print(f"Summary: {passed}/{len(cases)} passed")
    return 0 if passed == len(cases) else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("case_file", type=Path)
    parser.add_argument("--mock", action="store_true", help="Run against built-in mock device")
    parser.add_argument("--port", type=str, default=None, help="Serial port for hardware run")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=0.7)
    args = parser.parse_args()

    if not args.mock and not args.port:
        print("Choose either --mock or --port", file=sys.stderr)
        return 2
    if args.mock and args.port:
        print("Use only one of --mock or --port", file=sys.stderr)
        return 2

    cases = load_cases(args.case_file)

    if args.mock:
        return run(cases, MockDevice())

    dev = SerialDevice(port=args.port, baud=args.baud, timeout_s=args.timeout)
    try:
        return run(cases, dev)
    finally:
        dev.close()


if __name__ == "__main__":
    raise SystemExit(main())
