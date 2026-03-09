# P4 - PWM Control and Basic Control Algorithm

## Goal
Control output via PWM and evaluate a simple control algorithm.

## Scope
- PWM output control
- Basic P/PI/PID-style loop (or filtered control)
- Response measurement and tuning

## Inheritance Base (from P1/P2)
P4 is pre-wired to use `Tutorials/common/firmware`:
- `uart_ring.c/.h` (UART RX ISR ring buffer)
- `cmd_line.c/.h` (line command parser)

Current scaffold commands:
- `PING`, `HELP`, `VERSION`, `GET STAT`

## Build/Test
```bash
make -C Tutorials/P4 build test
```
