#!/usr/bin/env python3
"""Host-side SPI frame checks for W5100S command format."""

from __future__ import annotations


def build_read_frame(addr: int) -> list[int]:
    if not (0 <= addr <= 0xFFFF):
        raise ValueError("addr out of range")
    return [(addr >> 8) & 0xFF, addr & 0xFF, 0xF0, 0x00]


def build_write_frame(addr: int, value: int) -> list[int]:
    if not (0 <= addr <= 0xFFFF):
        raise ValueError("addr out of range")
    if not (0 <= value <= 0xFF):
        raise ValueError("value out of range")
    return [(addr >> 8) & 0xFF, addr & 0xFF, 0x0F, value]


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
    assert build_read_frame(0x0080) == [0x00, 0x80, 0xF0, 0x00]
    assert build_write_frame(0x0001, 0xAB) == [0x00, 0x01, 0x0F, 0xAB]

    # CLI response format compatibility checks
    f = parse_ok_fields("OK VERR=0x51 EXPECT=0x51 DETECTED=1")
    assert f["VERR"] == "0x51"
    assert f["DETECTED"] == "1"

    print("PASS: W5100S SPI frame composition checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
