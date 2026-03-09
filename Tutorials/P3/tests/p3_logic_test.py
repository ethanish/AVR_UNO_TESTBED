#!/usr/bin/env python3
"""P3 host-side logic checks (hysteresis and event logging model)."""

from __future__ import annotations


def run_hysteresis(samples, low=480, recover=520):
    active = False
    min_adc = 1023
    start_idx = None
    events = []

    for i, v in enumerate(samples):
        if not active and v <= low:
            active = True
            min_adc = v
            start_idx = i
            continue

        if active:
            if v < min_adc:
                min_adc = v
            if v >= recover:
                events.append({"start": start_idx, "end": i, "min": min_adc, "duration": i - start_idx})
                active = False
                min_adc = 1023
                start_idx = None

    return events, active


def ring_push(cap, seq):
    log = []
    for item in seq:
        log.append(item)
        if len(log) > cap:
            log.pop(0)
    return log


def main() -> int:
    # One sag event with hysteresis
    samples = [700, 650, 500, 470, 430, 410, 450, 490, 515, 530, 600]
    events, active = run_hysteresis(samples)
    assert not active
    assert len(events) == 1
    assert events[0]["min"] == 410
    assert events[0]["duration"] == 6

    # No event because recover threshold is not reached
    samples = [700, 650, 470, 430, 450, 500, 510]
    events, active = run_hysteresis(samples)
    assert active
    assert len(events) == 0

    # Ring log capacity behavior
    log = ring_push(4, [1, 2, 3, 4, 5, 6])
    assert log == [3, 4, 5, 6]

    print("PASS: P3 hysteresis/event-log model checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
