# P5 - I2C EEPROM Event Logger

## Goal
Persist events in external I2C EEPROM across power cycles.

## Scope
- I2C/TWI communication
- Fixed-size event record format
- Data integrity checks (e.g., CRC)

## Structure
- `firmware/`: AVR firmware source
- `tests/`: Read/write and retention checks
- `docs/`: Memory map and record format
