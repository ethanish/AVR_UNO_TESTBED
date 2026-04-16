#!/usr/bin/env python3
"""Interactive UART testbed for P4 PWM/PI control."""

from __future__ import annotations

import argparse
import os
import select
import sys
import time
from collections import deque

COMMAND_HELP = [
    "GET PWM                    -> mode/target/duty/meas",
    "GET POWER                  -> percent-based power view",
    "GET CTRL                   -> KP/KI/LPF/period/settle cfg",
    "GET RESP                   -> peak/overshoot/settling",
    "GET STAT                   -> UART ISR/tick/control count",
    "SET MODE AUTO|MANUAL",
    "SET POWER <0..100>",
    "SET TARGET_PCT <0..100>",
    "SET TARGET <0..255>",
    "SET DUTY <0..255>",
    "SET GAIN KP <0..8192>",
    "SET GAIN KI <0..8192>",
    "SET LPF <1..100>",
    "SET PERIOD <1..200>",
    "SET SETTLE TOL <0..40>",
    "SET SETTLE NEED <1..200>",
    "RESET RESP | RESET IACC",
    "r                          -> one-shot status refresh",
    "q / quit                   -> exit",
]

KEYS = (
    "MODE",
    "TARGET_PCT",
    "DUTY_PCT",
    "MEAS_PCT",
    "TARGET",
    "DUTY",
    "MEAS",
    "KP_Q10",
    "KI_Q10",
    "I_ACC",
    "LPF_PCT",
    "PERIOD_MS",
    "TOL",
    "NEED",
    "PEAK",
    "MIN",
    "OVERSHOOT",
    "SETTLED",
    "SETTLING_MS",
    "CTRL_COUNT",
    "UART_RX_ISR",
    "TICK_MS",
)


def require_pyserial():
    try:
        import serial  # type: ignore
    except Exception as exc:
        raise RuntimeError("pyserial is required. Install with: pip install pyserial") from exc
    return serial


def parse_ok_fields(line: str):
    if not line.startswith("OK "):
        return {}
    out = {}
    for tok in line[3:].split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def apply_fields(status: dict[str, str], fields: dict[str, str]):
    for k in KEYS:
        if k in fields:
            status[k] = fields[k]


def clear_screen() -> None:
    sys.stdout.write("\033[2J\033[H")


def draw_ui(port: str, baud: int, status: dict[str, str], last_rx: str, log: deque[str]) -> None:
    clear_screen()
    print("P4 PWM/PI Interactive Testbed")
    print(f"Port: {port}  Baud: {baud}")
    print("PWM : MODE={MODE} TARGET={TARGET} DUTY={DUTY} MEAS={MEAS}".format(**status))
    print("PWR : TARGET={TARGET_PCT}% DUTY={DUTY_PCT}% MEAS={MEAS_PCT}%".format(**status))
    print(
        "CTRL: KP={KP_Q10} KI={KI_Q10} I_ACC={I_ACC} LPF={LPF_PCT}% PERIOD={PERIOD_MS}ms".format(
            **status
        )
    )
    print("SET : TOL={TOL} NEED={NEED} CTRL_COUNT={CTRL_COUNT}".format(**status))
    print(
        "RESP: PEAK={PEAK} MIN={MIN} OVERSHOOT={OVERSHOOT} SETTLED={SETTLED} SETTLING_MS={SETTLING_MS}".format(
            **status
        )
    )
    print("STAT: UART_RX_ISR={UART_RX_ISR} TICK_MS={TICK_MS}".format(**status))
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
    send_cmd(ser, log, "GET PWM")
    send_cmd(ser, log, "GET POWER")
    send_cmd(ser, log, "GET CTRL")
    send_cmd(ser, log, "GET RESP")
    send_cmd(ser, log, "GET STAT")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle-seconds", type=float, default=2.0)
    parser.add_argument("--refresh-seconds", type=float, default=0.4)
    parser.add_argument("--auto-refresh-status", action="store_true")
    parser.add_argument("--auto-refresh-period", type=float, default=1.0)
    args = parser.parse_args()

    serial = require_pyserial()
    ser = serial.Serial(port=args.port, baudrate=args.baud, timeout=0.05)

    status = {k: "N/A" for k in KEYS}
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
                cmd = sys.stdin.readline().strip()
                low = cmd.lower()
                if low in ("q", "quit"):
                    return 0
                if low == "r":
                    one_shot_refresh(ser, log)
                elif cmd:
                    send_cmd(ser, log, cmd)

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
