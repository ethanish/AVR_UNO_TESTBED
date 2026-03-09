#!/usr/bin/env python3
"""Interactive UART testbed for P1.

Features:
- Always-on command dashboard at top
- Live status area (last response, UART RX ISR count, ADC0 as power proxy)
- User interaction loop: send commands manually and see board responses

Usage:
  python3 tests/polling_testbed.py --port /dev/tty.usbmodemXXXX
"""

from __future__ import annotations

import argparse
import os
import select
import sys
import time
from collections import deque


COMMAND_HELP = [
    "PING                -> PONG",
    "HELP                -> command summary",
    "VERSION             -> firmware version",
    "GET STAT            -> UART RX ISR count",
    "GET ADC 0           -> raw ADC (power proxy)",
    "GET LED / SET LED 0|1",
    "GET PWM / SET PWM 0..255",
    "r                   -> one-shot status refresh (GET ADC 0 + GET STAT)",
    "Type 'q' or 'quit' to exit",
]


def require_pyserial():
    try:
        import serial  # type: ignore
    except Exception as exc:  # pragma: no cover
        raise RuntimeError("pyserial is required. Install with: pip install pyserial") from exc
    return serial


def clear_screen() -> None:
    sys.stdout.write("\033[2J\033[H")


def draw_ui(
    port: str, baud: int, adc0: str, uart_rx_isr: str, last_rx: str, log: deque[str]
) -> None:
    clear_screen()
    print("P1 UART Interactive Testbed")
    print(
        f"Port: {port}  Baud: {baud}  ADC0(power): {adc0}  UART_RX_ISR: {uart_rx_isr}"
    )
    print("-" * 72)
    print("Commands")
    for row in COMMAND_HELP:
        print(f"  {row}")
    print("-" * 72)
    print(f"Last RX: {last_rx}")
    print("Recent Log")
    for line in log:
        print(f"  {line}")
    print("-" * 72)
    print("Input command then Enter > ", end="", flush=True)


def drain_startup(ser, settle_s: float) -> list[str]:
    lines: list[str] = []
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
    if not line:
        return None
    return line


def parse_status_line(line: str, adc0: str, uart_rx_isr: str):
    if line.startswith("OK ADC="):
        return line.split("=", 1)[1], uart_rx_isr
    if line.startswith("OK UART_RX_ISR="):
        return adc0, line.split("=", 1)[1]
    return adc0, uart_rx_isr


def poll_status(ser, adc0: str, uart_rx_isr: str):
    ser.write(b"GET ADC 0\n")
    deadline = time.time() + 0.3
    while time.time() < deadline:
        line = read_line_nowait(ser)
        if line is None:
            continue
        adc0, uart_rx_isr = parse_status_line(line, adc0, uart_rx_isr)
        if line.startswith("OK ADC="):
            break

    ser.write(b"GET STAT\n")
    deadline = time.time() + 0.3
    while time.time() < deadline:
        line = read_line_nowait(ser)
        if line is None:
            continue
        adc0, uart_rx_isr = parse_status_line(line, adc0, uart_rx_isr)
        if line.startswith("OK UART_RX_ISR="):
            break

    return adc0, uart_rx_isr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/tty.usbmodem11201)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle-seconds", type=float, default=2.0)
    parser.add_argument("--refresh-seconds", type=float, default=0.5)
    parser.add_argument(
        "--auto-poll-status",
        action="store_true",
        help="Periodically send GET ADC 0 / GET STAT",
    )
    parser.add_argument("--poll-seconds", type=float, default=1.0)
    args = parser.parse_args()

    serial = require_pyserial()
    ser = serial.Serial(port=args.port, baudrate=args.baud, timeout=0.05)

    log: deque[str] = deque(maxlen=12)
    last_rx = "(none)"
    adc0 = "N/A"
    uart_rx_isr = "N/A"

    try:
        time.sleep(0.2)
        boot_lines = drain_startup(ser, args.settle_seconds)
        if boot_lines:
            for line in boot_lines[-3:]:
                log.append(f"RX> {line}")
                last_rx = line

        next_refresh = 0.0
        next_poll = 0.0

        while True:
            now = time.time()

            if args.auto_poll_status and now >= next_poll:
                adc0, uart_rx_isr = poll_status(ser, adc0, uart_rx_isr)
                next_poll = now + args.poll_seconds

            while True:
                line = read_line_nowait(ser)
                if line is None:
                    break
                last_rx = line
                adc0, uart_rx_isr = parse_status_line(line, adc0, uart_rx_isr)
                log.append(f"RX> {line}")

            ready, _, _ = select.select([sys.stdin], [], [], 0)
            if ready:
                user_cmd = sys.stdin.readline().strip()
                if user_cmd.lower() in ("q", "quit"):
                    return 0
                if user_cmd.lower() == "r":
                    adc0, uart_rx_isr = poll_status(ser, adc0, uart_rx_isr)
                    log.append("TX> GET ADC 0")
                    log.append("TX> GET STAT")
                elif user_cmd:
                    ser.write((user_cmd + "\n").encode("ascii", errors="ignore"))
                    log.append(f"TX> {user_cmd}")

            if now >= next_refresh:
                draw_ui(args.port, args.baud, adc0, uart_rx_isr, last_rx, log)
                next_refresh = now + args.refresh_seconds

            time.sleep(0.02)
    finally:
        ser.close()
        # Keep terminal output clean after ANSI redraw loop.
        if os.isatty(sys.stdout.fileno()):
            print()


if __name__ == "__main__":
    raise SystemExit(main())
