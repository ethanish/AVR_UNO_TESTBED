# P4 - PWM Control and Basic Control Algorithm

## Goal
`Docs/deepresearch.pdf`의 P4 요구사항에 맞춰 PWM 출력과 간단한 제어 루프를 구현하고,
튜닝/응답 지표를 UART 명령으로 확인합니다.

## Scope Implemented
- Timer2 Fast PWM 출력 (`OC2A`, Arduino D11/PB3)
- Timer1 1ms tick + 주기 제어 루프 (기본 10ms)
- PI 제어기 (`KP_Q10`, `KI_Q10`, 고정소수점)
- 1차 저역통과 모델 기반 측정값(`MEAS`) 갱신
- 응답 지표: `PEAK`, `OVERSHOOT`, `SETTLED`, `SETTLING_MS`

## Commands (UART 115200)
- `PING` -> `PONG`
- `HELP` -> command list
- `VERSION` -> `OK VERSION=P4-1.0`
- `GET STAT` -> `UART_RX_ISR`, `TICK_MS`, `CTRL_COUNT`
- `GET PWM` -> `MODE`, `TARGET`, `DUTY`, `MEAS`
- `GET CTRL` -> gain/period/filter/settle params
- `GET RESP` -> response metrics
- `SET MODE AUTO|MANUAL`
- `SET TARGET <0..255>`
- `SET DUTY <0..255>` (manual mode)
- `SET GAIN KP <0..8192>`
- `SET GAIN KI <0..8192>`
- `SET LPF <1..100>`
- `SET PERIOD <1..200>`
- `SET SETTLE TOL <0..40>`
- `SET SETTLE NEED <1..200>`
- `RESET RESP`
- `RESET IACC`

## Inheritance Base (from P1/P2/P3)
P4는 공통 UART/CLI 계층을 재사용합니다.
- `Tutorials/common/firmware/uart_ring.c/.h`
- `Tutorials/common/firmware/cmd_line.c/.h`

## Build/Test
```bash
make -C Tutorials/P4 build test
```

## Interactive Hardware Testbed (CLI-like)
```bash
make -C Tutorials/P4 test-hw PORT=/dev/tty.usbmodemXXXX
```

## Quick Validation

### A) Physical PWM Output Check (LED)
이 단계는 "PWM 출력 핀(D11)이 실제로 바뀌는지" 확인합니다.

1. `SET MODE MANUAL`
2. `SET DUTY 0`
3. `SET DUTY 64`
4. `SET DUTY 128`
5. `SET DUTY 255`

기대 결과:
- LED 밝기가 단계적으로 증가/감소하면 PWM 출력은 정상입니다.
- LED만으로는 고속 PWM 파형 자체를 직접 보긴 어렵고, 평균 밝기 변화로 간접 확인합니다.

### B) Control Loop Logic Check (Internal Model)
이 단계는 "PI 제어 로직이 목표값으로 수렴하는지" 확인합니다.

1. `GET PWM`, `GET CTRL`, `GET RESP`
2. `SET MODE AUTO`
3. `RESET RESP`
4. `SET TARGET 200`
5. 1~2초 뒤 `GET PWM`, `GET RESP`

기대 결과:
- `GET PWM`에서 `MEAS`가 `TARGET` 근처로 이동
- `GET RESP`에서 `SETTLED=1`

주의:
- 현재 P4는 외부 센서 피드백이 아니라 내부 모델 기반(`MEAS`) 제어 검증입니다.
- 즉, `GET RESP` 지표는 LED 광량 실측값이 아니라 펌웨어 내부 계산값입니다.

### C) Tuning Comparison (Optional)
1. 기본 게인으로 `SET TARGET 200` 수행 후 `GET RESP` 기록
2. `SET GAIN KP 1400`
3. `SET GAIN KI 120`
4. 다시 `SET TARGET 200` 후 `GET RESP` 비교

<video src="./Pulse_with_LED.MOV" controls width="400">
  비디오를 지원하지 않는 브라우저입니다.
</video>
