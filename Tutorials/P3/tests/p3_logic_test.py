#!/usr/bin/env python3
"""P3 placeholder tests for inherited command protocol contract."""

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
    sample = "OK ADC=512"
    fields = parse_ok_fields(sample)
    assert fields.get("ADC") == "512"

    sample = "OK UART_RX_ISR=12"
    fields = parse_ok_fields(sample)
    assert fields.get("UART_RX_ISR") == "12"

    print("PASS: P3 base protocol parser checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
