MCU=atmega328p
F_CPU=16000000UL
BAUD=115200
CC=avr-gcc
OBJCOPY=avr-objcopy
CFLAGS=-mmcu=$(MCU) -Os -Wall -Wextra -std=c11 -DF_CPU=$(F_CPU)

PORT?=/dev/tty.usbmodem11201
PROGRAMMER?=arduino
UPLOAD_BAUD?=115200

TARGET=p1
FW_DIR=firmware
BUILD_DIR=out
ELF=$(BUILD_DIR)/$(TARGET).elf
HEX=$(BUILD_DIR)/$(TARGET).hex

.PHONY: all build flash test test-hw clean

all: build

build: $(HEX)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(ELF): $(FW_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex -R .eeprom $< $@

flash: $(HEX)
	avrdude -p m328p -c $(PROGRAMMER) -P $(PORT) -b $(UPLOAD_BAUD) -D -U flash:w:$(HEX):i

test:
	python3 tests/runner.py --mock tests/cases/smoke.json

test-hw:
	python3 tests/runner.py --port $(PORT) tests/cases/smoke.json

clean:
	rm -rf $(BUILD_DIR)
