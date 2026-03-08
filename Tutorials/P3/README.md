# P3 - ADC Voltage Sag Detection and Logging

## Goal
Detect simulated voltage sag events from ADC input and log event metadata.

## Scope
- ADC sampling pipeline
- Threshold + hysteresis event detector
- Event logging over UART

## Structure
- `firmware/`: AVR firmware source
- `tests/`: Event simulation and checks
- `docs/`: Event definition and parameter notes
