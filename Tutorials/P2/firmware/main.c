#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/atomic.h>

#include "../../common/firmware/cmd_line.h"
#include "../../common/firmware/uart_ring.h"

#define BAUD 115200UL
#define CMD_BUF_SIZE 64

/* Timer1: 16MHz / 64 = 250kHz (4us per timer count)
 * OCR1A=249 => 1000Hz compare match (1ms tick)
 */
#define TIMER1_TICK_US 4UL
#define TIMER1_OCR1A_1MS 249U

static volatile uint32_t tick_ms = 0;
static volatile bool flag_1ms = false;
static volatile bool flag_10ms = false;
static volatile bool flag_100ms = false;
static volatile uint16_t rate10_ms = 10;
static volatile uint16_t rate100_ms = 100;
static volatile uint16_t accum10_ms = 0;
static volatile uint16_t accum100_ms = 0;

static volatile uint32_t task1_count = 0;
static volatile uint32_t task10_count = 0;
static volatile uint32_t task100_count = 0;

static volatile uint16_t jitter_min_counts = 0xFFFF;
static volatile uint16_t jitter_max_counts = 0;
static volatile uint32_t jitter_sum_counts = 0;
static volatile uint32_t jitter_samples = 0;

static volatile uint8_t led_state = 0;
static volatile bool led_auto_mode = true;

static void timer1_init_1ms_tick(void) {
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
    OCR1A = TIMER1_OCR1A_1MS;
    TCNT1 = 0;
    TIMSK1 = _BV(OCIE1A);
}

ISR(TIMER1_COMPA_vect) {
    uint16_t latency_counts = TCNT1;
    uint16_t local_rate10;
    uint16_t local_rate100;

    if (latency_counts < jitter_min_counts) {
        jitter_min_counts = latency_counts;
    }
    if (latency_counts > jitter_max_counts) {
        jitter_max_counts = latency_counts;
    }
    jitter_sum_counts += latency_counts;
    jitter_samples += 1U;

    tick_ms += 1U;
    flag_1ms = true;
    local_rate10 = rate10_ms;
    local_rate100 = rate100_ms;

    accum10_ms += 1U;
    if (accum10_ms >= local_rate10) {
        accum10_ms = 0;
        flag_10ms = true;
    }

    accum100_ms += 1U;
    if (accum100_ms >= local_rate100) {
        accum100_ms = 0;
        flag_100ms = true;
    }
}

static bool consume_flag(volatile bool *flag) {
    bool v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        v = *flag;
        *flag = false;
    }
    return v;
}

static void io_init(void) {
    DDRB |= _BV(PB5);
    PORTB &= (uint8_t)~_BV(PB5);
}

static void led_apply(uint8_t on) {
    if (on) {
        PORTB |= _BV(PB5);
    } else {
        PORTB &= (uint8_t)~_BV(PB5);
    }
    led_state = on ? 1U : 0U;
}

static void task_1ms(void) { task1_count += 1U; }
static void task_10ms(void) { task10_count += 1U; }

static void task_100ms(void) {
    task100_count += 1U;
    if (led_auto_mode) {
        led_apply((uint8_t)!led_state);
    }
}

static void reset_jitter_stats(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        jitter_min_counts = 0xFFFF;
        jitter_max_counts = 0;
        jitter_sum_counts = 0;
        jitter_samples = 0;
    }
}

static uint32_t snapshot_tick_ms(void) {
    uint32_t snap;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        snap = tick_ms;
    }
    return snap;
}

static void snapshot_rates(uint16_t *r10, uint16_t *r100) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        *r10 = rate10_ms;
        *r100 = rate100_ms;
    }
}

static void respond_ok_u32(const char *k, uint32_t v) {
    char msg[80];
    snprintf(msg, sizeof(msg), "OK %s=%lu", k, (unsigned long)v);
    uart_writeln(msg);
}

static void respond_err(const char *e) {
    char msg[80];
    snprintf(msg, sizeof(msg), "ERR %s", e);
    uart_writeln(msg);
}

static void respond_jitter_stats(void) {
    uint16_t min_c;
    uint16_t max_c;
    uint32_t sum_c;
    uint32_t samples;
    uint32_t avg_c;
    char msg[120];

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        min_c = jitter_min_counts;
        max_c = jitter_max_counts;
        sum_c = jitter_sum_counts;
        samples = jitter_samples;
    }

    if (samples == 0U) {
        uart_writeln("OK JITTER_MIN_US=0 JITTER_MAX_US=0 JITTER_AVG_US=0 SAMPLES=0");
        return;
    }

    avg_c = sum_c / samples;
    snprintf(msg,
             sizeof(msg),
             "OK JITTER_MIN_US=%lu JITTER_MAX_US=%lu JITTER_AVG_US=%lu SAMPLES=%lu",
             (unsigned long)min_c * TIMER1_TICK_US,
             (unsigned long)max_c * TIMER1_TICK_US,
             (unsigned long)avg_c * TIMER1_TICK_US,
             (unsigned long)samples);
    uart_writeln(msg);
}

static void respond_task_counts(void) {
    uint32_t c1, c10, c100;
    char msg[120];

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        c1 = task1_count;
        c10 = task10_count;
        c100 = task100_count;
    }

    snprintf(msg,
             sizeof(msg),
             "OK TASK1=%lu TASK10=%lu TASK100=%lu",
             (unsigned long)c1,
             (unsigned long)c10,
             (unsigned long)c100);
    uart_writeln(msg);
}

static void respond_rates(void) {
    uint16_t r10, r100;
    char msg[80];

    snapshot_rates(&r10, &r100);
    snprintf(msg, sizeof(msg), "OK RATE10_MS=%u RATE100_MS=%u", r10, r100);
    uart_writeln(msg);
}

static void respond_led_status(void) {
    char msg[80];
    uint8_t state;
    bool auto_mode;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        state = led_state;
        auto_mode = led_auto_mode;
    }
    snprintf(msg, sizeof(msg), "OK LED=%u LED_MODE=%s", state, auto_mode ? "AUTO" : "MANUAL");
    uart_writeln(msg);
}

static void set_rate_ms(uint8_t target, uint16_t value_ms) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (target == 10U) {
            rate10_ms = value_ms;
            accum10_ms = 0;
        } else {
            rate100_ms = value_ms;
            accum100_ms = 0;
        }
    }
}

static void stress_block_interrupts_ms(uint16_t block_ms) {
    uint16_t m;
    uint16_t i;

    cli();
    for (m = 0; m < block_ms; m++) {
        for (i = 0; i < 16000U; i++) {
            __asm__ __volatile__("nop");
        }
    }
    sei();
}

static void handle_command(char *line) {
    char *argv[4] = {0};
    uint8_t argc = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok != NULL && argc < 4U) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }

    if (argc == 0U) return;

    if (strcmp(argv[0], "PING") == 0) {
        uart_writeln("PONG");
        return;
    }
    if (strcmp(argv[0], "HELP") == 0) {
        uart_writeln("OK CMDS=PING,HELP,VERSION,GET,SET,RESET,STRESS");
        return;
    }
    if (strcmp(argv[0], "VERSION") == 0) {
        uart_writeln("OK VERSION=P2-1.0");
        return;
    }

    if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "TICK") == 0) {
            respond_ok_u32("TICK_MS", snapshot_tick_ms());
            return;
        }
        if (strcmp(argv[1], "JITTER") == 0) {
            respond_jitter_stats();
            return;
        }
        if (strcmp(argv[1], "TASKS") == 0) {
            respond_task_counts();
            return;
        }
        if (strcmp(argv[1], "RATE") == 0) {
            respond_rates();
            return;
        }
        if (strcmp(argv[1], "LED") == 0) {
            respond_led_status();
            return;
        }

        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "SET") == 0) {
        long target;
        long value;

        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "LED") == 0) {
            if (argc < 3U) {
                respond_err("BAD_ARG");
                return;
            }
            if (strcmp(argv[2], "AUTO") == 0) {
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { led_auto_mode = true; }
                respond_led_status();
                return;
            }
            if (strcmp(argv[2], "MANUAL") == 0) {
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { led_auto_mode = false; }
                respond_led_status();
                return;
            }

            value = strtol(argv[2], NULL, 10);
            if (!(value == 0L || value == 1L)) {
                respond_err("BAD_RANGE");
                return;
            }
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { led_auto_mode = false; }
            led_apply((uint8_t)value);
            respond_led_status();
            return;
        }

        if (strcmp(argv[1], "RATE") == 0) {
            if (argc < 4U) {
                respond_err("BAD_ARG");
                return;
            }
            target = strtol(argv[2], NULL, 10);
            value = strtol(argv[3], NULL, 10);
            if (!(target == 10L || target == 100L)) {
                respond_err("BAD_TARGET");
                return;
            }
            if (value < 1L || value > 5000L) {
                respond_err("BAD_RANGE");
                return;
            }
            set_rate_ms((uint8_t)target, (uint16_t)value);
            respond_rates();
            return;
        }

        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "RESET") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }
        if (strcmp(argv[1], "JITTER") == 0) {
            reset_jitter_stats();
            uart_writeln("OK JITTER_RESET=1");
            return;
        }
        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "STRESS") == 0) {
        long block_ms;
        char msg[80];

        if (argc < 3U) {
            respond_err("BAD_ARG");
            return;
        }
        if (strcmp(argv[1], "BLOCK") != 0) {
            respond_err("BAD_TARGET");
            return;
        }
        block_ms = strtol(argv[2], NULL, 10);
        if (block_ms < 1L || block_ms > 200L) {
            respond_err("BAD_RANGE");
            return;
        }
        stress_block_interrupts_ms((uint16_t)block_ms);
        snprintf(msg, sizeof(msg), "OK STRESS_BLOCK_MS=%ld", block_ms);
        uart_writeln(msg);
        return;
    }

    respond_err("BAD_CMD");
}

int main(void) {
    char cmd_buf[CMD_BUF_SIZE];
    uint8_t cmd_len = 0;

    io_init();
    uart_init(BAUD);
    timer1_init_1ms_tick();
    sei();

    uart_writeln("OK BOOT=P2");

    while (1) {
        cmd_line_result_t r;

        if (consume_flag(&flag_1ms)) {
            task_1ms();
        }
        if (consume_flag(&flag_10ms)) {
            task_10ms();
        }
        if (consume_flag(&flag_100ms)) {
            task_100ms();
        }

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
