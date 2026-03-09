#!/usr/bin/env python3
"""Host-side sanity checks for P2 scheduler/jitter math."""

from __future__ import annotations


def simulate_ticks(n: int, rate10: int = 10, rate100: int = 100):
    t1 = 0
    t10 = 0
    t100 = 0
    a10 = 0
    a100 = 0
    for tick_ms in range(1, n + 1):
        t1 += 1
        a10 += 1
        a100 += 1
        if a10 >= rate10:
            t10 += 1
            a10 = 0
        if a100 >= rate100:
            t100 += 1
            a100 = 0
    return t1, t10, t100


def counts_to_us(counts: int, tick_us: int = 4) -> int:
    return counts * tick_us


def main() -> int:
    t1, t10, t100 = simulate_ticks(1000)
    assert (t1, t10, t100) == (1000, 100, 10), (t1, t10, t100)

    t1, t10, t100 = simulate_ticks(1000, rate10=20, rate100=250)
    assert (t1, t10, t100) == (1000, 50, 4), (t1, t10, t100)

    assert counts_to_us(0) == 0
    assert counts_to_us(3) == 12
    assert counts_to_us(25) == 100

    print("PASS: scheduler ratios (fixed+variable rates) and jitter unit conversion")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
