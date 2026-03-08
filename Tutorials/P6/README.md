# P6 - Watchdog, Error Handling, and Crash Dump

## Goal
Design a robust fail-safe path with watchdog reset handling and fault context logs.

## Scope
- Watchdog configuration
- Error classification and safe fallback
- Reset-cause reporting and minimal crash dump

## Structure
- `firmware/`: AVR firmware source
- `tests/`: Fault injection and recovery checks
- `docs/`: Reset policy and error taxonomy
