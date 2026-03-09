#include "uart_ring.h"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/atomic.h>

#ifndef F_CPU
#error "F_CPU must be defined"
#endif

#define RX_BUF_SIZE 128

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;
static volatile bool rx_overflow = false;
static volatile uint16_t uart_rx_isr_count = 0;

void uart_init(uint32_t baud) {
    uint16_t ubrr;

    ubrr = (uint16_t)((F_CPU / (8UL * baud)) - 1UL);
    UCSR0A = _BV(U2X0);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);
    UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

ISR(USART_RX_vect) {
    uint8_t next_head;
    uint8_t data = UDR0;

    next_head = (uint8_t)((rx_head + 1U) % RX_BUF_SIZE);
    if (next_head == rx_tail) {
        rx_overflow = true;
        return;
    }

    rx_buf[rx_head] = data;
    rx_head = next_head;
    uart_rx_isr_count += 1U;
}

int uart_read_byte_nonblock(uint8_t *out) {
    if (rx_head == rx_tail) {
        return 0;
    }
    *out = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1U) % RX_BUF_SIZE);
    return 1;
}

bool uart_rx_overflowed_and_clear(void) {
    bool was;
    was = rx_overflow;
    rx_overflow = false;
    return was;
}

uint16_t uart_rx_isr_count_snapshot(void) {
    uint16_t snap;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        snap = uart_rx_isr_count;
    }
    return snap;
}

void uart_write_char(char c) {
    while (!(UCSR0A & _BV(UDRE0))) {
    }
    UDR0 = (uint8_t)c;
}

void uart_write(const char *s) {
    while (*s) {
        uart_write_char(*s++);
    }
}

void uart_writeln(const char *s) {
    uart_write(s);
    uart_write("\r\n");
}
