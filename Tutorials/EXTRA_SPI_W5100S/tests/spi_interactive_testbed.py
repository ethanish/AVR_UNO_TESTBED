#!/usr/bin/env python3
"""Interactive UART console for EXTRA SPI W5100S firmware."""

from __future__ import annotations

import argparse
import os
import select
import sys
import time
from collections import deque

COMMAND_HELP = [
    "GET CHIP                   -> read VERR (expect 0x51)",
    "GET STAT                   -> UART/SPI counters",
    "GET REG 0x0000             -> read MR",
    "SET REG 0x0000 0x80        -> write example",
    "DUMP 0x0000 8              -> block read",
    "SPI RAW 0x00 0x80 0xF0 0x00",
    "r                          -> one-shot refresh",
    "q / quit                   -> exit",
]


def require_pyserial():
    try:
        import serial  # type: ignore
    except Exception as exc:
        raise RuntimeError("pyserial is required. Install with: pip install pyserial") from exc
    return serial


def clear_screen() -> None:
    sys.stdout.write("\033[2J\033[H")


def draw_ui(port: str, baud: int, last_rx: str, log: deque[str]) -> None:
    clear_screen()
    print("EXTRA SPI W5100S Interactive Testbed")
    print(f"Port: {port}  Baud: {baud}")
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
    send_cmd(ser, log, "GET CHIP")
    send_cmd(ser, log, "GET STAT")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle-seconds", type=float, default=1.5)
    parser.add_argument("--refresh-seconds", type=float, default=0.4)
    args = parser.parse_args()

    serial = require_pyserial()
    ser = serial.Serial(port=args.port, baudrate=args.baud, timeout=0.05)

    log: deque[str] = deque(maxlen=16)
    last_rx = "(none)"

    try:
        time.sleep(0.2)
        deadline = time.time() + args.settle_seconds
        while time.time() < deadline:
            line = read_line_nowait(ser)
            if line is None:
                continue
            last_rx = line
            log.append(f"RX> {line}")

        next_draw = 0.0
        one_shot_refresh(ser, log)
        while True:
            now = time.time()

            while True:
                line = read_line_nowait(ser)
                if line is None:
                    break
                last_rx = line
                log.append(f"RX> {line}")

            ready, _, _ = select.select([sys.stdin], [], [], 0)
            if ready:
                cmd = sys.stdin.readline().strip()
                low = cmd.lower()
                if low in ("q", "quit"):
                    return 0
                if low == "r":
                    one_shot_refresh(ser, log)
                elif cmd:
                    send_cmd(ser, log, cmd)

            if now >= next_draw:
                draw_ui(args.port, args.baud, last_rx, log)
                next_draw = now + args.refresh_seconds

            time.sleep(0.02)
    finally:
        ser.close()
        if os.isatty(sys.stdout.fileno()):
            print()


if __name__ == "__main__":
    raise SystemExit(main())
