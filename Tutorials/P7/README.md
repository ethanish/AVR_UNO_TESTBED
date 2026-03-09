# P7 - ISP Programming and Fuse/Clock/BOD Practice

## Goal
Practice ISP programming and safe fuse/clock/BOD configuration workflows.

## Scope
- ISP upload workflow
- Fuse read/write verification
- Clock and BOD impact checks

## Inheritance Base (from P1/P2)
P7 is pre-wired to use `Tutorials/common/firmware`:
- `uart_ring.c/.h` (UART RX ISR ring buffer)
- `cmd_line.c/.h` (line command parser)

Current scaffold commands:
- `PING`, `HELP`, `VERSION`, `GET STAT`

## Build/Test
```bash
make -C Tutorials/P7 build test
```
