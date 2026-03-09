# P3 - ADC Voltage Sag Detection and Logging

## Goal
Detect simulated voltage sag events from ADC input and log event metadata.

## Inheritance Base (from P1/P2)
P3 uses the shared firmware layer at `Tutorials/common/firmware`:
- `uart_ring.c/.h`: UART RX ISR ring buffer + TX helpers
- `cmd_line.c/.h`: line-based command parser

This ensures P3 inherits the same serial command handling behavior used by P1/P2.

## Current Commands (P3 base scaffold)
- `PING` -> `PONG`
- `HELP` -> command list
- `VERSION` -> `OK VERSION=P3-0.1`
- `GET ADC <0..5>` -> `OK ADC=<0..1023>`
- `GET STAT` -> `OK UART_RX_ISR=<count>`

## Build/Test
```bash
make -C Tutorials/P3 build test
```
