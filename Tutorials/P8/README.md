# P8 - FPGA Mini Task (Optional)

## Goal
Complete a small FPGA-focused task complementary to MCU firmware workflow.

## Scope
- Minimal HDL module and simulation
- Interface or timing experiment
- Result documentation

## Inheritance Base (from P1/P2)
P8 is pre-wired to use `Tutorials/common/firmware` for companion MCU communication path:
- `uart_ring.c/.h` (UART RX ISR ring buffer)
- `cmd_line.c/.h` (line command parser)

Current scaffold commands:
- `PING`, `HELP`, `VERSION`, `GET STAT`

## Build/Test
```bash
make -C Tutorials/P8 build test
```
