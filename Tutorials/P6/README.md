# P6 - Watchdog, Error Handling, and Crash Dump

## Goal
Design a robust fail-safe path with watchdog reset handling and fault context logs.

## Scope
- Watchdog configuration
- Error classification and safe fallback
- Reset-cause reporting and minimal crash dump

## Inheritance Base (from P1/P2)
P6 is pre-wired to use `Tutorials/common/firmware`:
- `uart_ring.c/.h` (UART RX ISR ring buffer)
- `cmd_line.c/.h` (line command parser)

Current scaffold commands:
- `PING`, `HELP`, `VERSION`, `GET STAT`

## Build/Test
```bash
make -C Tutorials/P6 build test
```
