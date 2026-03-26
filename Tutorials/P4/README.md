# P4 - PWM Control and Basic Control Algorithm

## Goal
`Docs/deepresearch.pdf`의 P4 요구사항에 맞춰 PWM 출력과 간단한 제어 루프를 구현하고,
튜닝/응답 지표를 UART 명령으로 확인합니다.

## Scope Implemented
- Timer2 Fast PWM 출력 (`OC2A`, Arduino D11/PB3)
- Timer1 1ms tick + 주기 제어 루프 (기본 10ms)
- PI 제어기 (`KP_Q10`, `KI_Q10`, 고정소수점)
- 1차 저역통과 모델 기반 측정값(`MEAS`) 갱신
- 응답 지표: `PEAK`, `MIN`, `OVERSHOOT`, `SETTLED`, `SETTLING_MS`

## Commands (UART 115200)
- `PING` -> `PONG`
- `HELP` -> command list
- `VERSION` -> `OK VERSION=P4-1.0`
- `GET STAT` -> `UART_RX_ISR`, `TICK_MS`, `CTRL_COUNT`
- `GET PWM` -> `MODE`, `TARGET`, `DUTY`, `MEAS`
- `GET CTRL` -> gain/period/filter/settle params
- `GET RESP` -> response metrics (`PEAK`, `MIN`, `OVERSHOOT`, `SETTLED`, `SETTLING_MS`)
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

## Term Reference

### Core signal/control terms
- `PWM`:
  - Pulse Width Modulation의 약자입니다.
  - 디지털 핀을 매우 빠르게 ON/OFF 하되, ON 비율을 바꿔 평균 출력 세기를 조절하는 방식입니다.
  - P4에서는 Timer2가 `D11(PB3, OC2A)` 핀에 PWM을 출력합니다.
  - LED를 연결하면 깜빡임보다는 "평균 밝기 변화"로 보입니다.

- `DUTY`:
  - PWM duty cycle에 해당하는 출력 명령값입니다.
  - 범위는 `0..255`이고, `0`이면 항상 꺼짐에 가깝고 `255`면 항상 켜짐에 가깝습니다.
  - 중간값 예: `128`은 대략 50% duty에 해당합니다.
  - 코드에서는 실제 PWM 레지스터 `OCR2A`에 들어가는 값입니다.

- `TARGET`:
  - 제어기가 도달하려는 목표값입니다.
  - AUTO 모드에서는 제어기가 `MEAS`를 이 값에 가깝게 만들려고 `DUTY`를 자동 조정합니다.
  - MANUAL 모드에서는 목표값을 바꿔도 PWM 출력은 자동으로 따라가지 않습니다.

- `MEAS`:
  - measurement의 약자이며, 제어기가 현재 출력 상태를 "측정했다고 가정하는 값"입니다.
  - 중요한 점은 P4의 `MEAS`가 외부 센서 실측값이 아니라는 점입니다.
  - 현재 구현에서는 PWM 출력이 어떤 시스템을 거쳐 천천히 반영된다고 가정한 내부 1차 모델 값입니다.
  - 즉, `DUTY`가 바로 `MEAS`가 되지 않고, 몇 번의 제어 주기를 거쳐 서서히 따라갑니다.

- `AUTO`:
  - 자동 제어 모드입니다.
  - 제어 루프가 `TARGET - MEAS` 오차를 계산하고, 그 결과로 `DUTY`를 자동 변경합니다.
  - 이 모드에서 PI 제어기와 응답 지표가 의미를 가집니다.

- `MANUAL`:
  - 수동 출력 모드입니다.
  - 사용자가 `SET DUTY <n>`으로 PWM 값을 직접 넣습니다.
  - 이 모드는 제어 알고리즘보다 "D11 핀 PWM 출력 자체"를 확인할 때 유용합니다.

### Controller/tuning terms
- `PI`:
  - Proportional-Integral Controller의 약자입니다.
  - 현재 오차와 누적 오차를 함께 사용해서 출력을 정하는 기본 제어기입니다.
  - P4는 `P`와 `I`만 사용하고 `D`는 사용하지 않습니다.

- `KP_Q10`:
  - 비례 게인 `Kp`를 고정소수점(Q10) 형식으로 저장한 값입니다.
  - 값이 클수록 현재 오차에 더 빠르고 강하게 반응합니다.
  - 너무 크면 빠르게 반응하지만 진동이나 overshoot가 커질 수 있습니다.
  - `Q10`은 내부적으로 `1024`를 1.0처럼 취급하는 스케일입니다.
  - 예를 들어 `KP_Q10=1024`는 대략 `Kp=1.0`에 해당합니다.

- `KI_Q10`:
  - 적분 게인 `Ki`를 Q10 고정소수점으로 저장한 값입니다.
  - 오차를 시간에 따라 누적해서 정상상태 오차를 줄이는 역할을 합니다.
  - 너무 작으면 목표에 천천히 붙고, 너무 크면 누적 효과 때문에 흔들릴 수 있습니다.

- `I_ACC`:
  - integral accumulator, 즉 적분 누적값입니다.
  - 제어기가 매 주기마다 오차를 쌓아 두는 내부 상태입니다.
  - `KI_Q10`과 함께 출력 계산에 사용됩니다.
  - `RESET IACC`는 이 누적값을 0으로 되돌려 튜닝 비교를 쉽게 해줍니다.

- `LPF_PCT`:
  - 저역통과필터(low-pass filter) 강도를 퍼센트처럼 표현한 값입니다.
  - `MEAS`가 `DUTY`를 어느 정도 속도로 따라갈지를 정합니다.
  - 값이 크면 `MEAS`가 더 빨리 따라가고, 값이 작으면 더 천천히 반응합니다.
  - 실제 의미는 "센서 필터"라기보다는 "출력 시스템의 느린 응답을 흉내내는 모델 계수"에 가깝습니다.

- `PERIOD_MS`:
  - 제어 루프가 몇 ms마다 한 번 실행되는지 나타냅니다.
  - 기본값은 `10ms`입니다.
  - 값이 작을수록 더 자주 제어하고, 값이 크면 더 느리게 제어합니다.

### Response/measurement terms
- `PEAK`:
  - 응답이 진행되는 동안 기록된 `MEAS`의 최대값입니다.
  - 목표를 올리는 step 응답에서 최대 얼마나 높이 올라갔는지 볼 때 사용합니다.

- `MIN`:
  - 응답이 진행되는 동안 기록된 `MEAS`의 최소값입니다.
  - 목표를 내리는 step 응답에서 최소 얼마나 낮아졌는지 볼 때 사용합니다.

- `OVERSHOOT`:
  - 목표를 지나치게 넘거나 아래로 떨어진 정도를 뜻합니다.
  - 상승 step에서는 `PEAK - TARGET`으로 해석합니다.
  - 하강 step에서는 `TARGET - MIN`으로 해석합니다.
  - 값이 `0`이면 목표를 넘지 않고 도달했거나 아직 넘지 않았다는 뜻입니다.

- `SETTLED`:
  - 응답이 목표값 근처에 안정적으로 들어왔는지를 나타내는 플래그입니다.
  - `0`은 아직 안정화 전, `1`은 안정화 완료입니다.

- `SETTLING_MS`:
  - 안정화가 완료되기까지 걸린 시간(ms)입니다.
  - `SETTLED=1`이 되는 순간의 경과 시간이 저장됩니다.

- `TOL`:
  - settling tolerance입니다.
  - `MEAS`가 `TARGET`에서 얼마나 떨어져 있어도 "거의 도달했다"고 볼지를 정합니다.
  - 예를 들어 `TOL=4`이면 `TARGET ±4` 안으로 들어왔을 때 안정화 판정 후보가 됩니다.

- `NEED`:
  - settle 판정을 위해 tolerance 범위 안에 연속 몇 번 들어와야 하는지를 뜻합니다.
  - 예를 들어 `NEED=20`이면 제어 주기 20번 연속으로 조건을 만족해야 `SETTLED=1`이 됩니다.
  - 순간적으로 한 번 들어온 것과 진짜 안정화된 상태를 구분하기 위한 값입니다.

### Status/debug terms
- `TICK_MS`:
  - Timer1 1ms tick 기준으로 증가하는 시스템 시간입니다.
  - 부팅 후 몇 ms가 지났는지 보는 내부 시간 카운터입니다.

- `CTRL_COUNT`:
  - 제어 루프가 실제로 몇 번 실행됐는지 나타냅니다.
  - `PERIOD_MS`가 10이면 대략 10ms마다 1씩 증가합니다.

- `UART_RX_ISR`:
  - UART 수신 인터럽트가 처리한 바이트 수입니다.
  - 사용자가 명령을 보내거나 테스트베드가 자동 조회를 하면 증가합니다.
  - 제어 성능 값이 아니라 통신 활동량을 보는 보조 지표입니다.

### Command semantics
- `GET PWM`:
  - 현재 제어 모드와 목표값, 실제 PWM 출력값, 내부 측정값을 한 줄로 확인하는 명령입니다.

- `GET CTRL`:
  - 현재 튜닝 파라미터와 제어 설정을 확인하는 명령입니다.

- `GET RESP`:
  - 마지막 step 응답에 대한 품질 지표를 확인하는 명령입니다.
  - LED 밝기 자체를 측정하는 명령이 아니라, 내부 모델 기준 응답 품질을 보여줍니다.

- `RESET RESP`:
  - 응답 지표 기록을 현재 시점 기준으로 다시 시작합니다.
  - 새 목표값을 주기 전 응답 비교를 깔끔하게 할 때 사용합니다.

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
- 상승 step에서는 `PEAK`, 하강 step에서는 `MIN`이 함께 기록되어 overshoot 해석이 일관되게 유지됩니다.

### C) Tuning Comparison (Optional)
1. 기본 게인으로 `SET TARGET 200` 수행 후 `GET RESP` 기록
2. `SET GAIN KP 1400`
3. `SET GAIN KI 120`
4. 다시 `SET TARGET 200` 후 `GET RESP` 비교