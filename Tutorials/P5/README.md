# P5 - I2C EEPROM Event Logger

## Goal
Persist events in external I2C EEPROM across power cycles.

## Scope
- I2C/TWI communication
- Fixed-size event record format
- Data integrity checks (e.g., CRC)

## Inheritance Base (from P1/P2)
P5 is pre-wired to use `Tutorials/common/firmware`:
- `uart_ring.c/.h` (UART RX ISR ring buffer)
- `cmd_line.c/.h` (line command parser)

Current scaffold commands:
- `PING`, `HELP`, `VERSION`, `GET STAT`

## Build/Test
```bash
make -C Tutorials/P5 build test
```
