#ifndef CMD_LINE_H
#define CMD_LINE_H

#include <stdint.h>

typedef enum {
    CMD_LINE_NONE = 0,
    CMD_LINE_READY = 1,
    CMD_LINE_TOO_LONG = 2,
} cmd_line_result_t;

void cmd_line_reset(void);
cmd_line_result_t cmd_line_poll(char *buf, uint8_t cap, uint8_t *len);

#endif
