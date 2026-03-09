#include "cmd_line.h"

#include <stdint.h>

#include "uart_ring.h"

void cmd_line_reset(void) {
}

cmd_line_result_t cmd_line_poll(char *buf, uint8_t cap, uint8_t *len) {
    uint8_t ch;

    if (!uart_read_byte_nonblock(&ch)) {
        return CMD_LINE_NONE;
    }

    if (ch == '\n' || ch == '\r') {
        if (*len == 0U) {
            return CMD_LINE_NONE;
        }
        buf[*len] = '\0';
        *len = 0;
        return CMD_LINE_READY;
    }

    if (*len >= (uint8_t)(cap - 1U)) {
        *len = 0;
        return CMD_LINE_TOO_LONG;
    }

    buf[*len] = (char)ch;
    *len = (uint8_t)(*len + 1U);
    return CMD_LINE_NONE;
}
