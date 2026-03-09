#define F_CPU 16000000UL

#include <avr/interrupt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../common/firmware/cmd_line.h"
#include "../../common/firmware/uart_ring.h"

#define BAUD 115200UL
#define CMD_BUF_SIZE 64

static void respond_ok_u16(const char *k, uint16_t v) {
    char msg[80];
    snprintf(msg, sizeof(msg), "OK %s=%u", k, v);
    uart_writeln(msg);
}

static void respond_err(const char *e) {
    char msg[80];
    snprintf(msg, sizeof(msg), "ERR %s", e);
    uart_writeln(msg);
}

static void handle_command(char *line) {
    char *argv[3] = {0};
    uint8_t argc = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok != NULL && argc < 3U) {
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
        uart_writeln("OK CMDS=PING,HELP,VERSION,GET");
        return;
    }

    if (strcmp(argv[0], "VERSION") == 0) {
        uart_writeln("OK VERSION=P4-0.1");
        return;
    }

    if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "STAT") == 0) {
            respond_ok_u16("UART_RX_ISR", uart_rx_isr_count_snapshot());
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
    sei();
    uart_writeln("OK BOOT=P4");

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
