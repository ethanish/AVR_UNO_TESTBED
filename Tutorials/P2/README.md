# P2 - 타이머 기반 주기 태스크 + 지터 측정

## 목표
- Timer1 CTC 기반 1ms tick
- 1ms/10ms/100ms 소프트 스케줄러
- ISR 진입 지연 기반 지터(min/max/avg) 계측

## 디렉터리
- `firmware/`: AVR 펌웨어 소스
- `tests/`: 호스트 측 로직 검증 스크립트
- `docs/`: 타이밍 예산/측정 메모

## 공통 레이어 (P1 계승)
P2는 `Tutorials/common/firmware`를 통해 P1의 UART/CLI 기반을 그대로 사용합니다.
- `uart_ring.c/.h`: UART RX ISR 링버퍼
- `cmd_line.c/.h`: 라인 명령 파서

즉, `P1 -> P2 -> P3`로 갈수록 기능은 확장되지만 통신 베이스는 동일하게 유지됩니다.

## 명령 프로토콜 (UART 115200)
- `PING` -> `PONG`
- `HELP` -> 지원 명령 목록
- `VERSION` -> `OK VERSION=P2-1.0`
- `GET RATE` -> `OK RATE10_MS=<ms> RATE100_MS=<ms>`
- `SET RATE 10 <ms>` -> `OK RATE10_MS=<ms> RATE100_MS=<ms>`
- `SET RATE 100 <ms>` -> `OK RATE10_MS=<ms> RATE100_MS=<ms>`
- `GET LED` -> `OK LED=<0|1> LED_MODE=<AUTO|MANUAL>`
- `SET LED 0|1` -> `OK LED=<0|1> LED_MODE=MANUAL`
- `SET LED AUTO|MANUAL` -> `OK LED=<0|1> LED_MODE=<AUTO|MANUAL>`
- `STRESS BLOCK <1..200>` -> 지정한 시간(ms) 동안 인터럽트를 막아 지터 엣지케이스 유도
- `GET TICK` -> `OK TICK_MS=<ms>`
- `GET TASKS` -> `OK TASK1=<n> TASK10=<n> TASK100=<n>`
- `GET JITTER` -> `OK JITTER_MIN_US=<n> JITTER_MAX_US=<n> JITTER_AVG_US=<n> SAMPLES=<n>`
- `RESET JITTER` -> `OK JITTER_RESET=1`

## 실행 방법
1. 빌드
```bash
make -C Tutorials/P2 build
```

2. 업로드
```bash
make -C Tutorials/P2 flash PORT=/dev/tty.usbmodemXXXX
```

3. 호스트 로직 테스트
```bash
make -C Tutorials/P2 test
```

4. 인터랙티브 테스트베드 (시각화)
```bash
make -C Tutorials/P2 test-interactive-hw PORT=/dev/tty.usbmodemXXXX
```

특징:
- 상단 상태 패널에 `RATE10/100`, `TICK_MS`, `TASK1/10/100`, `JITTER(min/max/avg/samples)` 실시간 표시
- 상단 상태 패널에 `LED(state/mode)` 실시간 표시
- 명령 입력 결과를 하단 로그에서 즉시 확인
- `r` 입력 시 상태 1회 갱신(`GET RATE`, `GET TICK`, `GET TASKS`, `GET JITTER`)
- `RESET JITTER` 입력 후 `GET JITTER`로 변화 확인 가능
- `SET RATE 10 20`, `SET RATE 100 250`처럼 런타임 주기 변경 가능
- `SET LED 1`, `SET LED 0`, `SET LED AUTO`로 LED 동작 모드 변경 가능
- `edge1`: `GET TICK` burst를 보내 UART 부하 엣지케이스 생성
- `edge2`: `STRESS BLOCK 50`으로 인터럽트 지연 엣지케이스 생성
- `q` 또는 `quit`로 종료

명령 문법 주의:
- `SET RATE 100`은 잘못된 명령입니다. 값이 필요합니다.
- 올바른 예: `SET RATE 100 250`

구버전 펌웨어 체크:
- `GET RATE`가 `ERR BAD_TARGET`이면 보드에 구버전 바이너리가 올라간 상태일 수 있습니다.
- 아래 순서로 최신 펌웨어를 다시 올리세요.
```bash
make -C Tutorials/P2 build
make -C Tutorials/P2 flash PORT=/dev/tty.usbmodemXXXX
```

자동 주기 갱신이 필요하면:
```bash
python3 Tutorials/P2/tests/p2_interactive_testbed.py --port /dev/tty.usbmodemXXXX --auto-refresh-status
```

## 확인 포인트
- `GET TASKS`에서 1:10:100 비율이 유지되는지
- UART 트래픽이 많을 때 `GET JITTER` 분포가 어떻게 변하는지
- 100ms 태스크로 LED(PB5)가 토글되는지
