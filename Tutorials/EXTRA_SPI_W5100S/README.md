# EXTRA - SPI Communication with W5100S (WIZnet Ethernet HAT)

## Goal
Implement and verify the SPI communication layer for W5100S-focused bring-up.
This extra track intentionally focuses on SPI transaction correctness (register read/write),
not full TCP/IP socket operation.

## Target
- Module: WIZnet Ethernet HAT (Pi Pico form factor)
- Ethernet chip: W5100S

## Scope Implemented
- AVR SPI master initialization (Mode 0, fosc/16)
- W5100S 4-byte SPI access
  - `[ADDR_H][ADDR_L][CTRL][DATA]`
  - Read control byte: `0xF0`
  - Write control byte: `0x0F`
- UART CLI commands for direct register access
- Host-side protocol tests for frame composition

## Important Electrical Note
Arduino UNO is 5V logic, many Pi Pico accessory boards are 3.3V logic.
Before wiring directly, verify the HAT input tolerance and use level shifting if needed.

## Suggested Wiring (SPI only)
- UNO D13 (SCK)  -> HAT SCLK
- UNO D11 (MOSI) -> HAT MOSI
- UNO D12 (MISO) -> HAT MISO
- UNO D10 (CS)   -> HAT CSn
- UNO GND        -> HAT GND
- UNO 3.3V/5V    -> HAT VCC (check board requirement first)

## Commands (UART 115200)
- `PING` -> `PONG`
- `HELP`
- `VERSION`
- `GET STAT` -> `UART_RX_ISR`, `SPI_XFER`
- `GET CHIP` -> reads `VERR` (`0x0080`), expected `0x51`
- `GET REG <addr>`
- `SET REG <addr> <value>`
- `DUMP <start_addr> <len 1..16>`
- `SPI RAW <b0> [b1..b7]` (manual full-duplex transaction)

## Build / Test / Flash
```bash
make -C Tutorials/EXTRA_SPI_W5100S build test
make -C Tutorials/EXTRA_SPI_W5100S flash PORT=/dev/tty.usbmodemXXXX
```

## Interactive Hardware CLI
```bash
make -C Tutorials/EXTRA_SPI_W5100S test-hw PORT=/dev/tty.usbmodemXXXX
```

## Minimal Bring-up Flow
1. `PING`
2. `GET STAT`
3. `GET CHIP`
4. If not detected, try `SPI RAW 0x00 0x80 0xF0 0x00`
5. `GET REG 0x0000` (MR)
6. `DUMP 0x0000 8`

Expected for real W5100S:
- `GET CHIP` should include `VERR=0x51` and `DETECTED=1`

## Limitation
Without actual module attached, SPI read values are undefined (often `0xFF`),
but CLI command handling and frame generation remain verifiable.
