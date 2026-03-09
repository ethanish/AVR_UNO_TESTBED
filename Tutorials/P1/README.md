# P1 - UART 명령 프로토콜 + PC 자동 테스트 러너

## 목표
`deepresearch.pdf`의 P1 요구사항에 맞춰 다음을 구현합니다.
- 링버퍼 기반 UART 수신
- 텍스트 명령 파서와 에러 코드
- JSON 테스트 케이스 기반 자동 테스트 러너

## 현재 구현 범위
- Firmware:
  - `PING`, `HELP`, `VERSION`
  - `SET/GET LED`
  - `SET/GET PWM`
  - `GET ADC <ch>`
  - 에러 코드: `BAD_CMD`, `BAD_ARG`, `BAD_TARGET`, `BAD_RANGE`, `LINE_TOO_LONG`, `RX_OVERFLOW`
- PC Runner:
  - `tests/cases/smoke.json` 입력
  - `--mock` 모드(하드웨어 없이 검증)
  - `--port` 모드(실보드 시리얼 검증)

## 디렉터리
- `firmware/`: AVR 펌웨어
- `tests/`: 테스트 러너 + 케이스
- `docs/`: 프로토콜/테스트 문서

## 사용 방법
1. 빌드
```bash
make -C Tutorials/P1 build
```

2. mock 테스트
```bash
make -C Tutorials/P1 test
```

3. 보드 업로드
```bash
make -C Tutorials/P1 flash PORT=/dev/tty.usbmodemXXXX
```

4. 하드웨어 테스트
```bash
make -C Tutorials/P1 test-hw PORT=/dev/tty.usbmodemXXXX
```
