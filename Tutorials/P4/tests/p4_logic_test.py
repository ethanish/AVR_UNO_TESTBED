#!/usr/bin/env python3
"""P4 host-side control loop checks (PI + response metrics)."""

from __future__ import annotations


def clamp_u8(v: int) -> int:
    return max(0, min(255, v))


class P4Model:
    def __init__(self, kp_q10=1024, ki_q10=64, alpha_pct=25, settle_tol=4, settle_need=20):
        self.target = 128
        self.duty = 0
        self.meas = 0
        self.kp_q10 = kp_q10
        self.ki_q10 = ki_q10
        self.alpha_pct = alpha_pct
        self.i_acc = 0
        self.mode_auto = True

        self.peak = 0
        self.min_meas = 0
        self.start_meas = 0
        self.overshoot = 0
        self.settle_tol = settle_tol
        self.settle_need = settle_need
        self.settle_count = 0
        self.settled = False

    def reset_response(self):
        self.start_meas = self.meas
        self.peak = self.meas
        self.min_meas = self.meas
        self.overshoot = 0
        self.settle_count = 0
        self.settled = False

    def set_target(self, t: int):
        self.target = clamp_u8(t)
        self.reset_response()

    def step(self):
        step_up = self.target >= self.start_meas
        delta = self.duty - self.meas
        adjust = (self.alpha_pct * delta) // 100
        if adjust == 0 and delta != 0:
            adjust = 1 if delta > 0 else -1
        self.meas = clamp_u8(self.meas + adjust)

        if self.mode_auto:
            err = self.target - self.meas
            self.i_acc = max(-4096, min(4096, self.i_acc + err))
            u = ((self.kp_q10 * err) + (self.ki_q10 * self.i_acc)) // 1024
            self.duty = clamp_u8(u)

        self.peak = max(self.peak, self.meas)
        self.min_meas = min(self.min_meas, self.meas)
        if step_up:
            self.overshoot = max(0, self.peak - self.target)
        else:
            self.overshoot = max(0, self.target - self.min_meas)
        err_abs = abs(self.target - self.meas)
        if err_abs <= self.settle_tol:
            self.settle_count += 1
            if self.settle_count >= self.settle_need:
                self.settled = True
        else:
            self.settle_count = 0


def main() -> int:
    m = P4Model()
    m.set_target(180)
    for _ in range(400):
        m.step()
    assert abs(m.meas - 180) <= 4
    assert m.settled

    m = P4Model(kp_q10=8192, ki_q10=8192)
    m.set_target(255)
    for _ in range(30):
        m.step()
        assert 0 <= m.duty <= 255

    m = P4Model(settle_tol=2, settle_need=12)
    m.set_target(60)
    for _ in range(20):
        m.step()
    assert not m.settled
    m.set_target(200)
    for _ in range(200):
        m.step()
    assert m.settled

    m = P4Model(kp_q10=1200, ki_q10=90)
    m.set_target(220)
    for _ in range(220):
        m.step()
    high_meas = m.meas
    assert high_meas >= 216
    m.set_target(80)
    for _ in range(220):
        m.step()
    assert abs(m.meas - 80) <= 4
    assert m.min_meas <= high_meas
    assert m.overshoot >= 0

    print("PASS: P4 PI control/response model checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
