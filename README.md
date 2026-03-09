# AVR_UNO_TESTBED

HOST: MAC OS (Apple M1 / Sonoma 14.3)

Board: ARDUINO UNO R3 (ATmega328P)
> https://docs.arduino.cc/learn/
> https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf

## avrdude

`avrdude`는 AVR MCU(예: ATmega328P)에 펌웨어를 업로드/검증할 때 사용하는 표준 커맨드라인 도구입니다.

- 이 저장소에서는 `Makefile`의 `flash` 타깃에서 사용됩니다.
- 기본적으로 `*.hex` 파일을 보드 플래시에 기록합니다.

예시:
```bash
make -C Tutorials/P1 flash PORT=/dev/tty.usbmodemXXXX
```

설치 확인:
```bash
avrdude -v
```

## Project Roadmap

This repository follows the practical roadmap summarized from `Docs/deepresearch.pdf`.

- `P0`: Basic bring-up and build/upload sanity checks
- `P1`: UART command protocol and PC test runner
- `P2`: Periodic scheduler and jitter measurement
- `P3`: ADC voltage sag detection and logging
- `P4`: PWM control and basic control algorithm
- `P5`: I2C EEPROM event logger
- `P6`: Watchdog, error handling, and crash dump
- `P7`: ISP programming and fuse/clock/BOD practice
- `P8`: FPGA mini task (optional)

## Directory Layout

- `Tutorials/P0/`: existing baseline code
- `Tutorials/P1/` to `Tutorials/P8/`: per-project folders with `README.md`, `firmware/`, `tests/`, `docs/`
