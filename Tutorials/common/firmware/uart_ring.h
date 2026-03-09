#ifndef UART_RING_H
#define UART_RING_H

#include <stdbool.h>
#include <stdint.h>

void uart_init(uint32_t baud);
int uart_read_byte_nonblock(uint8_t *out);
bool uart_rx_overflowed_and_clear(void);
uint16_t uart_rx_isr_count_snapshot(void);

void uart_write_char(char c);
void uart_write(const char *s);
void uart_writeln(const char *s);

#endif
