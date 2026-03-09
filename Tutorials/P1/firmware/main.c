#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/atomic.h>

#define BAUD 115200UL
#define UBRR_VALUE ((F_CPU / (8UL * BAUD)) - 1UL)

#define RX_BUF_SIZE 128
#define TX_BUF_SIZE 64
#define CMD_BUF_SIZE 64

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;
static volatile bool rx_overflow = false;
static volatile uint16_t uart_rx_isr_count = 0;

static uint8_t led_state = 0;
static uint8_t pwm_duty = 0;

static void uart_init(void) {
    UCSR0A = _BV(U2X0);
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE & 0xFF);
    UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

static void uart_putc(char c) {
    while (!(UCSR0A & _BV(UDRE0))) {
    }
    UDR0 = (uint8_t)c;
}

static void uart_write(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

static void uart_writeln(const char *s) {
    uart_write(s);
    uart_write("\r\n");
}

static int uart_getc_nonblock(uint8_t *out) {
    if (rx_head == rx_tail) {
        return 0;
    }
    *out = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1U) % RX_BUF_SIZE);
    return 1;
}

ISR(USART_RX_vect) {
    uint8_t next_head;
    uint8_t data = UDR0;

    next_head = (uint8_t)((rx_head + 1U) % RX_BUF_SIZE);
    if (next_head == rx_tail) {
        rx_overflow = true;
        return;
    }
    uart_rx_isr_count += 1;
    rx_buf[rx_head] = data;
    rx_head = next_head;
}

static void io_init(void) {
    DDRB |= _BV(PB5);
}

static void led_apply(void) {
    if (led_state) {
        PORTB |= _BV(PB5);
    } else {
        PORTB &= (uint8_t)~_BV(PB5);
    }
}

static void pwm_init(void) {
    DDRD |= _BV(PD3);
    TCCR2A = _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
    TCCR2B = _BV(CS21);
    OCR2B = pwm_duty;
}

static void pwm_apply(void) {
    OCR2B = pwm_duty;
}

static void adc_init(void) {
    ADMUX = _BV(REFS0);
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
}

static uint16_t adc_read(uint8_t channel) {
    ADMUX = (uint8_t)((ADMUX & 0xF0U) | (channel & 0x0FU) | _BV(REFS0));
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {
    }
    return ADC;
}

static void respond_ok_kv(const char *k, uint16_t v) {
    char msg[TX_BUF_SIZE];
    snprintf(msg, sizeof(msg), "OK %s=%u", k, v);
    uart_writeln(msg);
}

static void respond_err(const char *code) {
    char msg[TX_BUF_SIZE];
    snprintf(msg, sizeof(msg), "ERR %s", code);
    uart_writeln(msg);
}

static uint16_t uart_rx_isr_count_snapshot(void) {
    uint16_t snap;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        snap = uart_rx_isr_count;
    }
    return snap;
}

static void handle_command(char *line) {
    char *argv[4] = {0};
    uint8_t argc = 0;
    char *tok = strtok(line, " \t\r\n");
    
    while (tok != NULL && argc < 4U) {
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
        uart_writeln("OK CMDS=PING,HELP,VERSION,SET,GET (GET: LED,PWM,ADC,STAT)");
        return;
    }

    if (strcmp(argv[0], "VERSION") == 0) {
        uart_writeln("OK VERSION=P1-1.0");
        return;
    }

    if (strcmp(argv[0], "SET") == 0) {
        long val;

        if (argc < 3U) {
            respond_err("BAD_ARG");
            return;
        }

        val = strtol(argv[2], NULL, 10);

        if (strcmp(argv[1], "LED") == 0) {
            if (!(val == 0L || val == 1L)) {
                respond_err("BAD_RANGE");
                return;
            }
            led_state = (uint8_t)val;
            led_apply();
            respond_ok_kv("LED", led_state);
            return;
        }

        if (strcmp(argv[1], "PWM") == 0) {
            if (val < 0L || val > 255L) {
                respond_err("BAD_RANGE");
                return;
            }
            pwm_duty = (uint8_t)val;
            pwm_apply();
            respond_ok_kv("PWM", pwm_duty);
            return;
        }

        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "LED") == 0) {
            respond_ok_kv("LED", led_state);
            return;
        }

        if (strcmp(argv[1], "PWM") == 0) {
            respond_ok_kv("PWM", pwm_duty);
            return;
        }

        if (strcmp(argv[1], "ADC") == 0) {
            long ch;
            if (argc < 3U) {
                respond_err("BAD_ARG");
                return;
            }

            ch = strtol(argv[2], NULL, 10);
            if (ch < 0L || ch > 5L) {
                respond_err("BAD_RANGE");
                return;
            }

            respond_ok_kv("ADC", adc_read((uint8_t)ch));
            return;
        }

        if (strcmp(argv[1], "STAT") == 0) {
            respond_ok_kv("UART_RX_ISR", uart_rx_isr_count_snapshot());
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
    uint8_t ch;

    io_init();
    pwm_init();
    adc_init();
    uart_init();
    led_apply();
    pwm_apply();

    sei();
    uart_writeln("OK BOOT=P1");

    while (1) {
        if (rx_overflow) {
            rx_overflow = false;
            respond_err("RX_OVERFLOW");
        }

        if (!uart_getc_nonblock(&ch)) {
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            if (cmd_len > 0U) {
                cmd_buf[cmd_len] = '\0';
                handle_command(cmd_buf);
                cmd_len = 0;
            }
            continue;
        }

        if (cmd_len >= (CMD_BUF_SIZE - 1U)) {
            cmd_len = 0;
            respond_err("LINE_TOO_LONG");
            continue;
        }

        cmd_buf[cmd_len++] = (char)ch;
    }
}
