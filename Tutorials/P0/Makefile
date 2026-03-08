MCU=atmega328p
F_CPU=16000000UL
CC=avr-gcc
OBJCOPY=avr-objcopy
CFLAGS=-mmcu=$(MCU) -Os -DF_CPU=$(F_CPU)
PORT=/dev/tty.usbmodem11201
PROGRAMMER=arduino
BAUD=115200

TARGET=main

all: $(TARGET).hex

build: all

test:
	@echo "[P0] no automated tests yet"

$(TARGET).elf: $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

flash: $(TARGET).hex
	avrdude -p m328p -c $(PROGRAMMER) -P $(PORT) -b $(BAUD) -D -U flash:w:$(TARGET).hex:i

clean:
	rm -f *.elf *.hex
