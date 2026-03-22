MCU=atmega328p
F_CPU=16000000UL
BAUD=115200
CC=avr-gcc
OBJCOPY=avr-objcopy
CFLAGS=-mmcu=$(MCU) -Os -Wall -Wextra -std=c11 -DF_CPU=$(F_CPU)

PORT?=/dev/tty.usbmodem11201
PROGRAMMER?=arduino
UPLOAD_BAUD?=115200

TARGET=extra_spi_w5100s
FW_DIR=firmware
BUILD_DIR=out
COMMON_FW_DIR=../common/firmware
IOLIB_DIR=../common/ioLibrary_Driver
ELF=$(BUILD_DIR)/$(TARGET).elf
HEX=$(BUILD_DIR)/$(TARGET).hex
CFLAGS+=-D_WIZCHIP_=W5100S -DDATA_BUF_SIZE=128 -I$(IOLIB_DIR)/Ethernet -I$(IOLIB_DIR)/Application/loopback
SRCS=$(FW_DIR)/main.c $(FW_DIR)/netconfig.c \
	$(IOLIB_DIR)/Ethernet/socket.c $(IOLIB_DIR)/Ethernet/wizchip_conf.c \
	$(IOLIB_DIR)/Ethernet/W5100S/w5100s.c

.PHONY: all build flash test test-hw clean

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
	python3 tests/spi_protocol_test.py

test-hw:
	python3 tests/spi_interactive_testbed.py --port $(PORT)

clean:
	rm -rf $(BUILD_DIR)
