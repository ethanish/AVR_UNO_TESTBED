# AVR_UNO_TESTBED

HOST: MAC OS (Apple M1 / Sonoma 14.3)
Board: ARDUINO UNO R3 (ATmega328P)
> https://docs.arduino.cc/learn/
> https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf

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
