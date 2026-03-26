MCU=atmega328p
F_CPU=16000000UL
BAUD=115200
CC=avr-gcc
OBJCOPY=avr-objcopy
CFLAGS=-mmcu=$(MCU) -Os -Wall -Wextra -std=c11 -DF_CPU=$(F_CPU)

AUTO_PORT:=$(firstword $(sort $(wildcard /dev/tty.usbmodem* /dev/cu.usbmodem*)))
PORT?=$(AUTO_PORT)
PROGRAMMER?=arduino
UPLOAD_BAUD?=115200

TARGET=p4
FW_DIR=firmware
BUILD_DIR=out
COMMON_FW_DIR=../common/firmware
ELF=$(BUILD_DIR)/$(TARGET).elf
HEX=$(BUILD_DIR)/$(TARGET).hex
SRCS=$(FW_DIR)/main.c $(COMMON_FW_DIR)/uart_ring.c $(COMMON_FW_DIR)/cmd_line.c

.PHONY: all build flash test test-hw clean check-port

all: build

build: $(HEX)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(ELF): $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

check-port:
	@if [ -z "$(PORT)" ]; then \
		echo "No USB modem serial device found."; \
		echo "Run with PORT=/dev/tty.usbmodemXXXX or PORT=/dev/cu.usbmodemXXXX"; \
		exit 1; \
	fi
	@echo "Using PORT=$(PORT)"

flash: $(HEX) check-port
	avrdude -p m328p -c $(PROGRAMMER) -P $(PORT) -b $(UPLOAD_BAUD) -D -U flash:w:$(HEX):i

test:
	python3 tests/p4_logic_test.py

test-hw: check-port
	python3 tests/p4_interactive_testbed.py --port $(PORT) --auto-refresh-status

clean:
	rm -rf $(BUILD_DIR)
