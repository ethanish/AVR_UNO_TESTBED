# P3 - ADC Voltage Sag Detection and Logging

## Goal
Detect simulated voltage sag events from ADC input and log event metadata.

## Scope Implemented
- 1kHz ADC sampling (`ADC0`)
- Moving-average filter (`window=8`)
- Hysteresis sag detection (`TH_LOW`, `TH_RECOVER`)
- Event logging (`SAG_START`/`SAG_END`, duration/min value)

## Inheritance Base (from P1/P2)
P3 uses the shared firmware layer at `Tutorials/common/firmware`:
- `uart_ring.c/.h`: UART RX ISR ring buffer + TX helpers
- `cmd_line.c/.h`: line-based command parser

This keeps serial command handling behavior consistent across P1/P2/P3.

## Commands (UART 115200)
- `PING` -> `PONG`
- `HELP` -> command list
- `VERSION` -> `OK VERSION=P3-1.0`
- `GET ADC <0..5>` -> `OK ADC=<0..1023>`
- `GET STAT` -> `OK UART_RX_ISR=<count>`
- `GET SAMPLE` -> `OK RAW_ADC=<n> FILT_ADC=<n>`
- `GET CFG` -> `OK TH_LOW=<n> TH_RECOVER=<n>`
- `SET TH LOW <0..1023>`
- `SET TH RECOVER <0..1023>`
- `GET SAG` -> current sag status and duration
- `GET LOG COUNT|LAST|IDX <n>`
- `CLEAR LOG`

## Build/Test
```bash
make -C Tutorials/P3 build test
```

## Interactive Hardware Testbed
```bash
make -C Tutorials/P3 test-hw PORT=/dev/tty.usbmodemXXXX
```

## Validation Flow (Recommended)
1. `GET CFG`로 현재 threshold 확인
2. `SET TH LOW 480`, `SET TH RECOVER 520` (예시)
3. 가변저항으로 ADC 입력을 낮춰 `EVENT SAG_START` 확인
4. 다시 입력을 올려 `SAG_END` 로그 확인
5. `GET LOG LAST`와 `GET LOG COUNT`로 이벤트 기록 검증

## Jumper Test (A0 <-> GND/5V)
점퍼선으로 `A0`를 `GND`와 `5V` 사이에서 바꿔가며 sag 동작을 빠르게 검증할 수 있습니다.

사전 설정:
1. `SET TH LOW 480`
2. `SET TH RECOVER 520`
3. `CLEAR LOG`

진행 절차:
1. `A0 -> GND`로 연결
- `GET SAMPLE`: `RAW_ADC`, `FILT_ADC`가 낮아지는지 확인
- `GET SAG`: `SAG_ACTIVE=1` 전환 확인
- `GET LOG COUNT`: 아직 `0`이어도 정상(종료 전)

2. 1~2초 유지
- `GET SAG`: `SAG_DURATION_MS` 증가 확인
- `SAG_MIN_ADC`가 낮은 값으로 유지되는지 확인

3. `A0 -> 5V`로 전환
- `GET SAMPLE`: `RAW_ADC`, `FILT_ADC` 상승 확인
- `FILT_ADC >= TH_RECOVER` 시 sag 종료

4. 종료 이벤트 검증
- `GET LOG COUNT`: `1` 이상인지 확인
- `GET LOG LAST`: `DURATION_MS`, `MIN_ADC`, `START_ADC`, `END_ADC` 확인

해석 팁:
- `A0=GND`인데 `SAG_ACTIVE=1`이 안 되면 threshold/배선 점검
- `A0=5V` 전환 직후 종료가 지연될 수 있음(이동평균 필터 영향)
- `LOG_COUNT`가 안 늘면 recover 조건 미충족 상태일 가능성이 큼
