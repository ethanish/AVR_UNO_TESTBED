#!/usr/bin/env python3
"""Interactive UART testbed for P2 scheduler/jitter project."""

from __future__ import annotations

import argparse
import os
import select
import sys
import time
from collections import deque

COMMAND_HELP = [
    "PING                      -> PONG",
    "GET RATE                  -> current 10ms/100ms task rates",
    "SET RATE 10 <ms>          -> change 10ms task period (1..5000)",
    "SET RATE 100 <ms>         -> change 100ms task period (1..5000)",
    "GET LED                   -> LED state and mode",
    "SET LED 0|1               -> LED manual off/on",
    "SET LED AUTO|MANUAL       -> LED mode change",
    "STRESS BLOCK <1..200>     -> block interrupts intentionally",
    "GET TICK                  -> current tick(ms)",
    "GET TASKS                 -> 1ms/10ms/100ms task counters",
    "GET JITTER                -> jitter min/max/avg/samples",
    "RESET JITTER              -> reset jitter statistics",
    "r                         -> one-shot status refresh",
    "edge1                     -> UART burst edge case",
    "edge2                     -> interrupt-block edge case",
    "q / quit                  -> exit",
]


KEYS = (
    "TICK_MS",
    "RATE10_MS",
    "RATE100_MS",
    "LED",
    "LED_MODE",
    "TASK1",
    "TASK10",
    "TASK100",
    "JITTER_MIN_US",
    "JITTER_MAX_US",
    "JITTER_AVG_US",
    "SAMPLES",
)


def require_pyserial():
    try:
        import serial  # type: ignore
    except Exception as exc:  # pragma: no cover
        raise RuntimeError("pyserial is required. Install with: pip install pyserial") from exc
    return serial


def clear_screen() -> None:
    sys.stdout.write("\033[2J\033[H")


def parse_ok_fields(line: str):
    if not line.startswith("OK "):
        return {}
    fields = {}
    for token in line[3:].split():
        if "=" not in token:
            continue
        k, v = token.split("=", 1)
        fields[k] = v
    return fields


def apply_fields(status: dict[str, str], fields: dict[str, str]):
    for k in KEYS:
        if k in fields:
            status[k] = fields[k]


def draw_ui(port: str, baud: int, status: dict[str, str], last_rx: str, log: deque[str]) -> None:
    clear_screen()
    print("P2 Scheduler/Jitter Interactive Testbed")
    print(f"Port: {port}  Baud: {baud}")
    print("RATES(ms): rate10={RATE10_MS} rate100={RATE100_MS}".format(**status))
    print("LED: state={LED} mode={LED_MODE}".format(**status))
    print(
        "TICK_MS={TICK_MS}  TASK1={TASK1} TASK10={TASK10} TASK100={TASK100}".format(**status)
    )
    print(
        "JITTER(us): min={JITTER_MIN_US} max={JITTER_MAX_US} avg={JITTER_AVG_US} samples={SAMPLES}".format(
            **status
        )
    )
    print("-" * 78)
    print("Commands")
    for row in COMMAND_HELP:
        print(f"  {row}")
    print("-" * 78)
    print(f"Last RX: {last_rx}")
    print("Recent Log")
    for line in log:
        print(f"  {line}")
    print("-" * 78)
    print("Input command then Enter > ", end="", flush=True)


def drain_startup(ser, settle_s: float):
    lines = []
    deadline = time.time() + settle_s
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if line:
            lines.append(line)
    return lines


def read_line_nowait(ser):
    raw = ser.readline()
    if not raw:
        return None
    line = raw.decode("ascii", errors="replace").strip()
    return line or None


def send_cmd(ser, log: deque[str], cmd: str):
    ser.write((cmd + "\n").encode("ascii", errors="ignore"))
    log.append(f"TX> {cmd}")


def one_shot_refresh(ser, log: deque[str]):
    send_cmd(ser, log, "GET RATE")
    send_cmd(ser, log, "GET LED")
    send_cmd(ser, log, "GET TICK")
    send_cmd(ser, log, "GET TASKS")
    send_cmd(ser, log, "GET JITTER")


def run_edge1_burst(ser, log: deque[str]):
    # Intentionally generate serial RX pressure.
    for _ in range(200):
        send_cmd(ser, log, "GET TICK")
    send_cmd(ser, log, "GET JITTER")


def run_edge2_block(ser, log: deque[str]):
    # Force timer ISR latency by blocking global interrupts on device side.
    send_cmd(ser, log, "STRESS BLOCK 50")
    send_cmd(ser, log, "GET JITTER")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/tty.usbmodem11201)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle-seconds", type=float, default=2.0)
    parser.add_argument("--refresh-seconds", type=float, default=0.4)
    parser.add_argument("--auto-refresh-status", action="store_true")
    parser.add_argument("--auto-refresh-period", type=float, default=1.0)
    args = parser.parse_args()

    serial = require_pyserial()
    ser = serial.Serial(port=args.port, baudrate=args.baud, timeout=0.05)

    status = {k: "N/A" for k in KEYS}
    log: deque[str] = deque(maxlen=14)
    last_rx = "(none)"

    try:
        time.sleep(0.2)
        for line in drain_startup(ser, args.settle_seconds)[-4:]:
            last_rx = line
            log.append(f"RX> {line}")
            apply_fields(status, parse_ok_fields(line))

        next_draw = 0.0
        next_auto = 0.0

        while True:
            now = time.time()

            if args.auto_refresh_status and now >= next_auto:
                one_shot_refresh(ser, log)
                next_auto = now + args.auto_refresh_period

            while True:
                line = read_line_nowait(ser)
                if line is None:
                    break
                last_rx = line
                log.append(f"RX> {line}")
                apply_fields(status, parse_ok_fields(line))

            ready, _, _ = select.select([sys.stdin], [], [], 0)
            if ready:
                user_cmd = sys.stdin.readline().strip()
                lower = user_cmd.lower()

                if lower in ("q", "quit"):
                    return 0
                if lower == "r":
                    one_shot_refresh(ser, log)
                elif lower == "edge1":
                    run_edge1_burst(ser, log)
                elif lower == "edge2":
                    run_edge2_block(ser, log)
                elif user_cmd:
                    send_cmd(ser, log, user_cmd)

            if now >= next_draw:
                draw_ui(args.port, args.baud, status, last_rx, log)
                next_draw = now + args.refresh_seconds

            time.sleep(0.02)
    finally:
        ser.close()
        if os.isatty(sys.stdout.fileno()):
            print()


if __name__ == "__main__":
    raise SystemExit(main())
