MCU=atmega328p
F_CPU=16000000UL
BAUD=115200
CC=avr-gcc
OBJCOPY=avr-objcopy
CFLAGS=-mmcu=$(MCU) -Os -Wall -Wextra -std=c11 -DF_CPU=$(F_CPU)

PORT?=/dev/tty.usbmodem11201
PROGRAMMER?=arduino
UPLOAD_BAUD?=115200

TARGET=p7
FW_DIR=firmware
BUILD_DIR=out
COMMON_FW_DIR=../common/firmware
ELF=$(BUILD_DIR)/$(TARGET).elf
HEX=$(BUILD_DIR)/$(TARGET).hex
SRCS=$(FW_DIR)/main.c $(COMMON_FW_DIR)/uart_ring.c $(COMMON_FW_DIR)/cmd_line.c

.PHONY: all build flash test clean

all: build

build: $(HEX)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(ELF): $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

flash: $(HEX)
	avrdude -p m328p -c $(PROGRAMMER) -P $(PORT) -b $(UPLOAD_BAUD) -D -U flash:w:$(HEX):i

test:
	python3 tests/p7_logic_test.py

clean:
	rm -rf $(BUILD_DIR)
