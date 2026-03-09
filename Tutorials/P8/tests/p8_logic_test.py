#!/usr/bin/env python3
"""P8 scaffold checks for inherited UART protocol contract."""

from __future__ import annotations


def parse_ok_fields(line: str):
    if not line.startswith("OK "):
        return {}
    out = {}
    for token in line[3:].split():
        if "=" in token:
            k, v = token.split("=", 1)
            out[k] = v
    return out


def main() -> int:
    assert parse_ok_fields("OK UART_RX_ISR=12").get("UART_RX_ISR") == "12"
    print("PASS: P8 inherited protocol parser checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
