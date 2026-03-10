#define F_CPU 16000000UL

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/firmware/cmd_line.h"
#include "../../common/firmware/uart_ring.h"

#define BAUD 115200UL
#define CMD_BUF_SIZE 96

#define W5100S_REG_MR 0x0000U
#define W5100S_REG_VERR 0x0080U
#define W5100S_VERR_VALUE 0x51U

#define W5100S_SPI_CMD_READ 0xF0U
#define W5100S_SPI_CMD_WRITE 0x0FU

#define SPI_CS_DDR DDRB
#define SPI_CS_PORT PORTB
#define SPI_CS_PIN PB2

#define SPI_MOSI_DDR DDRB
#define SPI_MOSI_PIN PB3
#define SPI_MISO_DDR DDRB
#define SPI_MISO_PIN PB4
#define SPI_SCK_DDR DDRB
#define SPI_SCK_PIN PB5

static volatile uint32_t spi_xfer_count = 0;

static void respond_err(const char *e) {
    char msg[96];
    snprintf(msg, sizeof(msg), "ERR %s", e);
    uart_writeln(msg);
}

static void respond_ok_u32(const char *k, uint32_t v) {
    char msg[96];
    snprintf(msg, sizeof(msg), "OK %s=%lu", k, (unsigned long)v);
    uart_writeln(msg);
}

static int parse_long(const char *s, long *out) {
    char *end;
    long v = strtol(s, &end, 0);
    if (*s == '\0' || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

static void spi_cs_low(void) { SPI_CS_PORT &= (uint8_t)~_BV(SPI_CS_PIN); }
static void spi_cs_high(void) { SPI_CS_PORT |= _BV(SPI_CS_PIN); }

static void spi_init(void) {
    SPI_CS_DDR |= _BV(SPI_CS_PIN);
    SPI_MOSI_DDR |= _BV(SPI_MOSI_PIN);
    SPI_SCK_DDR |= _BV(SPI_SCK_PIN);
    SPI_MISO_DDR &= (uint8_t)~_BV(SPI_MISO_PIN);

    spi_cs_high();

    /* SPI master, mode 0, fosc/16 = 1MHz at 16MHz core */
    SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR0);
    SPSR = 0;
}

static uint8_t spi_xfer(uint8_t tx) {
    SPDR = tx;
    while (!(SPSR & _BV(SPIF))) {
    }
    spi_xfer_count += 1U;
    return SPDR;
}

static uint8_t w5100s_read_u8(uint16_t addr) {
    uint8_t v;
    spi_cs_low();
    spi_xfer((uint8_t)(addr >> 8));
    spi_xfer((uint8_t)(addr & 0xFFU));
    spi_xfer(W5100S_SPI_CMD_READ);
    v = spi_xfer(0x00U);
    spi_cs_high();
    return v;
}

static void w5100s_write_u8(uint16_t addr, uint8_t value) {
    spi_cs_low();
    spi_xfer((uint8_t)(addr >> 8));
    spi_xfer((uint8_t)(addr & 0xFFU));
    spi_xfer(W5100S_SPI_CMD_WRITE);
    spi_xfer(value);
    spi_cs_high();
}

static void w5100s_read_block(uint16_t start, uint8_t *out, uint8_t len) {
    uint8_t i;
    for (i = 0; i < len; i += 1U) {
        out[i] = w5100s_read_u8((uint16_t)(start + i));
    }
}

static void handle_spi_raw(char **argv, uint8_t argc) {
    uint8_t tx[8] = {0};
    uint8_t rx[8] = {0};
    uint8_t n;
    uint8_t i;
    long v;
    char msg[160];
    int written;

    if (argc < 3U) {
        respond_err("BAD_ARG");
        return;
    }

    n = (uint8_t)(argc - 2U);
    if (n > 8U) {
        respond_err("BAD_RANGE");
        return;
    }

    for (i = 0; i < n; i += 1U) {
        if (!parse_long(argv[2U + i], &v) || v < 0L || v > 255L) {
            respond_err("BAD_RANGE");
            return;
        }
        tx[i] = (uint8_t)v;
    }

    spi_cs_low();
    for (i = 0; i < n; i += 1U) {
        rx[i] = spi_xfer(tx[i]);
    }
    spi_cs_high();

    written = snprintf(msg, sizeof(msg), "OK SPI_RAW");
    for (i = 0; i < n && written > 0; i += 1U) {
        written += snprintf(msg + written,
                            (size_t)(sizeof(msg) - (size_t)written),
                            " TX%u=0x%02X RX%u=0x%02X",
                            i,
                            tx[i],
                            i,
                            rx[i]);
    }
    uart_writeln(msg);
}

static void handle_command(char *line) {
    char *argv[10] = {0};
    uint8_t argc = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok != NULL && argc < 10U) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }

    if (argc == 0U) {
        return;
    }

    if (strcmp(argv[0], "PING") == 0) {
        uart_writeln("PONG");
        return;
    }

    if (strcmp(argv[0], "HELP") == 0) {
        uart_writeln("OK CMDS=PING,HELP,VERSION,GET,SET,DUMP,SPI");
        return;
    }

    if (strcmp(argv[0], "VERSION") == 0) {
        uart_writeln("OK VERSION=EXTRA-SPI-W5100S-1.0");
        return;
    }

    if (strcmp(argv[0], "GET") == 0) {
        long addr;
        uint8_t value;
        char msg[128];

        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "STAT") == 0) {
            char stat[128];
            snprintf(stat,
                     sizeof(stat),
                     "OK UART_RX_ISR=%u SPI_XFER=%lu",
                     uart_rx_isr_count_snapshot(),
                     (unsigned long)spi_xfer_count);
            uart_writeln(stat);
            return;
        }

        if (strcmp(argv[1], "CHIP") == 0) {
            value = w5100s_read_u8(W5100S_REG_VERR);
            snprintf(msg,
                     sizeof(msg),
                     "OK VERR=0x%02X EXPECT=0x%02X DETECTED=%u",
                     value,
                     W5100S_VERR_VALUE,
                     (value == W5100S_VERR_VALUE) ? 1U : 0U);
            uart_writeln(msg);
            return;
        }

        if (strcmp(argv[1], "REG") == 0) {
            if (argc < 3U || !parse_long(argv[2], &addr) || addr < 0L || addr > 0xFFFFL) {
                respond_err("BAD_RANGE");
                return;
            }
            value = w5100s_read_u8((uint16_t)addr);
            snprintf(msg, sizeof(msg), "OK ADDR=0x%04lX VALUE=0x%02X", addr, value);
            uart_writeln(msg);
            return;
        }

        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "SET") == 0) {
        long addr;
        long value;
        char msg[128];

        if (argc < 4U) {
            respond_err("BAD_ARG");
            return;
        }
        if (strcmp(argv[1], "REG") != 0) {
            respond_err("BAD_TARGET");
            return;
        }

        if (!parse_long(argv[2], &addr) || !parse_long(argv[3], &value) || addr < 0L ||
            addr > 0xFFFFL || value < 0L || value > 0xFFL) {
            respond_err("BAD_RANGE");
            return;
        }

        w5100s_write_u8((uint16_t)addr, (uint8_t)value);
        snprintf(msg, sizeof(msg), "OK ADDR=0x%04lX WRITE=0x%02lX", addr, value);
        uart_writeln(msg);
        return;
    }

    if (strcmp(argv[0], "DUMP") == 0) {
        long start;
        long len_l;
        uint8_t buf[16];
        uint8_t i;
        uint8_t len;
        char msg[196];
        int written;

        if (argc < 3U || !parse_long(argv[1], &start) || !parse_long(argv[2], &len_l)) {
            respond_err("BAD_ARG");
            return;
        }
        if (start < 0L || start > 0xFFFFL || len_l < 1L || len_l > 16L ||
            (start + len_l - 1L) > 0xFFFFL) {
            respond_err("BAD_RANGE");
            return;
        }

        len = (uint8_t)len_l;
        w5100s_read_block((uint16_t)start, buf, len);

        written = snprintf(msg, sizeof(msg), "OK DUMP START=0x%04lX LEN=%u", start, len);
        for (i = 0; i < len && written > 0; i += 1U) {
            written += snprintf(msg + written,
                                (size_t)(sizeof(msg) - (size_t)written),
                                " B%u=0x%02X",
                                i,
                                buf[i]);
        }
        uart_writeln(msg);
        return;
    }

    if (strcmp(argv[0], "SPI") == 0) {
        if (argc >= 2U && strcmp(argv[1], "RAW") == 0) {
            handle_spi_raw(argv, argc);
            return;
        }
        respond_err("BAD_TARGET");
        return;
    }

    respond_err("BAD_CMD");
}

int main(void) {
    char cmd_buf[CMD_BUF_SIZE];
    uint8_t cmd_len = 0;

    uart_init(BAUD);
    spi_init();
    sei();

    uart_writeln("OK BOOT=EXTRA-SPI-W5100S");
    respond_ok_u32("MR_ADDR", W5100S_REG_MR);

    while (1) {
        cmd_line_result_t r;

        if (uart_rx_overflowed_and_clear()) {
            respond_err("RX_OVERFLOW");
        }

        r = cmd_line_poll(cmd_buf, CMD_BUF_SIZE, &cmd_len);
        if (r == CMD_LINE_NONE) {
            continue;
        }
        if (r == CMD_LINE_TOO_LONG) {
            respond_err("LINE_TOO_LONG");
            continue;
        }

        handle_command(cmd_buf);
    }
}
