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
#define CMD_BUF_SIZE 80

#define TIMER1_OCR1A_1MS 249U
#define SAMPLE_WINDOW 8U
#define EVENT_LOG_CAP 16U

typedef struct {
    uint32_t id;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t duration_ms;
    uint16_t start_adc;
    uint16_t end_adc;
    uint16_t min_adc;
} sag_event_t;

static volatile uint32_t tick_ms = 0;
static volatile bool sample_due = false;

static volatile uint16_t th_low = 480;
static volatile uint16_t th_recover = 520;

static volatile uint16_t raw_adc = 0;
static volatile uint16_t filt_adc = 0;
static volatile bool sag_active = false;
static volatile uint32_t sag_start_ms = 0;
static volatile uint16_t sag_start_adc = 0;
static volatile uint16_t sag_min_adc = 1023;

static uint16_t avg_hist[SAMPLE_WINDOW] = {0};
static uint8_t avg_idx = 0;
static uint8_t avg_count = 0;
static uint32_t avg_sum = 0;

static sag_event_t event_log[EVENT_LOG_CAP];
static volatile uint8_t log_head = 0;
static volatile uint8_t log_count = 0;
static volatile uint32_t next_event_id = 1;

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

static void timer1_init_1ms_tick(void) {
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
    OCR1A = TIMER1_OCR1A_1MS;
    TCNT1 = 0;
    TIMSK1 = _BV(OCIE1A);
}

ISR(TIMER1_COMPA_vect) {
    tick_ms += 1U;
    sample_due = true;
}

static bool consume_sample_due(void) {
    bool due;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        due = sample_due;
        sample_due = false;
    }
    return due;
}

static uint32_t snapshot_tick_ms(void) {
    uint32_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        v = tick_ms;
    }
    return v;
}

static void respond_err(const char *e) {
    char msg[96];
    snprintf(msg, sizeof(msg), "ERR %s", e);
    uart_writeln(msg);
}

static void respond_ok_u16(const char *k, uint16_t v) {
    char msg[96];
    snprintf(msg, sizeof(msg), "OK %s=%u", k, v);
    uart_writeln(msg);
}

static void respond_cfg(void) {
    uint16_t low;
    uint16_t recover;
    char msg[96];

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        low = th_low;
        recover = th_recover;
    }
    snprintf(msg, sizeof(msg), "OK TH_LOW=%u TH_RECOVER=%u", low, recover);
    uart_writeln(msg);
}

static void respond_sample(void) {
    uint16_t raw;
    uint16_t filt;
    char msg[96];

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        raw = raw_adc;
        filt = filt_adc;
    }
    snprintf(msg, sizeof(msg), "OK RAW_ADC=%u FILT_ADC=%u", raw, filt);
    uart_writeln(msg);
}

static void respond_sag_status(void) {
    bool active;
    uint32_t start_ms;
    uint32_t now_ms;
    uint32_t duration = 0;
    uint16_t min_adc;
    uint16_t filt;
    char msg[128];

    now_ms = snapshot_tick_ms();
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        active = sag_active;
        start_ms = sag_start_ms;
        min_adc = sag_min_adc;
        filt = filt_adc;
    }

    if (active && now_ms >= start_ms) {
        duration = now_ms - start_ms;
    }

    snprintf(msg,
             sizeof(msg),
             "OK SAG_ACTIVE=%u SAG_DURATION_MS=%lu SAG_MIN_ADC=%u FILT_ADC=%u",
             active ? 1U : 0U,
             (unsigned long)duration,
             min_adc,
             filt);
    uart_writeln(msg);
}

static void log_clear(void) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        log_head = 0;
        log_count = 0;
        next_event_id = 1;
    }
}

static void log_push(const sag_event_t *ev) {
    event_log[log_head] = *ev;
    log_head = (uint8_t)((log_head + 1U) % EVENT_LOG_CAP);
    if (log_count < EVENT_LOG_CAP) {
        log_count += 1U;
    }
}

static bool log_get_by_index(uint8_t idx, sag_event_t *out) {
    uint8_t count;
    uint8_t head;
    uint8_t pos;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        count = log_count;
        head = log_head;
    }

    if (idx >= count) {
        return false;
    }

    pos = (uint8_t)((head + EVENT_LOG_CAP - count + idx) % EVENT_LOG_CAP);
    *out = event_log[pos];
    return true;
}

static bool log_get_last(sag_event_t *out) {
    uint8_t count;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        count = log_count;
    }
    if (count == 0U) {
        return false;
    }
    return log_get_by_index((uint8_t)(count - 1U), out);
}

static void respond_log_count(void) {
    uint8_t count;
    char msg[64];

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        count = log_count;
    }
    snprintf(msg, sizeof(msg), "OK LOG_COUNT=%u", count);
    uart_writeln(msg);
}

static void respond_event(const sag_event_t *ev, const char *prefix) {
    char msg[180];
    snprintf(msg,
             sizeof(msg),
             "OK %s ID=%lu START_MS=%lu END_MS=%lu DURATION_MS=%lu MIN_ADC=%u START_ADC=%u END_ADC=%u",
             prefix,
             (unsigned long)ev->id,
             (unsigned long)ev->start_ms,
             (unsigned long)ev->end_ms,
             (unsigned long)ev->duration_ms,
             ev->min_adc,
             ev->start_adc,
             ev->end_adc);
    uart_writeln(msg);
}

static int parse_long(const char *s, long *out) {
    char *end;
    long v;
    v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

static void process_sample_tick(void) {
    uint16_t low;
    uint16_t recover;
    uint16_t sample;
    uint16_t filt;

    sample = adc_read(0);

    avg_sum -= avg_hist[avg_idx];
    avg_hist[avg_idx] = sample;
    avg_sum += sample;
    avg_idx = (uint8_t)((avg_idx + 1U) % SAMPLE_WINDOW);
    if (avg_count < SAMPLE_WINDOW) {
        avg_count += 1U;
    }

    filt = (uint16_t)(avg_sum / avg_count);

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        raw_adc = sample;
        filt_adc = filt;
        low = th_low;
        recover = th_recover;
    }

    if (!sag_active && filt <= low) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            sag_active = true;
            sag_start_ms = tick_ms;
            sag_start_adc = filt;
            sag_min_adc = filt;
        }
        uart_writeln("EVENT SAG_START");
        return;
    }

    if (sag_active) {
        if (filt < sag_min_adc) {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                sag_min_adc = filt;
            }
        }

        if (filt >= recover) {
            sag_event_t ev;
            uint32_t end_ms = snapshot_tick_ms();

            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                ev.id = next_event_id++;
                ev.start_ms = sag_start_ms;
                ev.end_ms = end_ms;
                ev.duration_ms = (end_ms >= sag_start_ms) ? (end_ms - sag_start_ms) : 0U;
                ev.start_adc = sag_start_adc;
                ev.end_adc = filt;
                ev.min_adc = sag_min_adc;
                sag_active = false;
                sag_start_ms = 0;
                sag_start_adc = 0;
                sag_min_adc = 1023;
            }

            log_push(&ev);
            respond_event(&ev, "SAG_END");
        }
    }
}

static void handle_command(char *line) {
    char *argv[5] = {0};
    uint8_t argc = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok != NULL && argc < 5U) {
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
        uart_writeln("OK CMDS=PING,HELP,VERSION,GET,SET,CLEAR");
        return;
    }

    if (strcmp(argv[0], "VERSION") == 0) {
        uart_writeln("OK VERSION=P3-1.0");
        return;
    }

    if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "ADC") == 0) {
            long ch;
            if (argc < 3U) {
                respond_err("BAD_ARG");
                return;
            }
            if (!parse_long(argv[2], &ch)) {
                respond_err("BAD_ARG");
                return;
            }
            if (ch < 0L || ch > 5L) {
                respond_err("BAD_RANGE");
                return;
            }
            respond_ok_u16("ADC", adc_read((uint8_t)ch));
            return;
        }

        if (strcmp(argv[1], "STAT") == 0) {
            respond_ok_u16("UART_RX_ISR", uart_rx_isr_count_snapshot());
            return;
        }

        if (strcmp(argv[1], "SAMPLE") == 0) {
            respond_sample();
            return;
        }

        if (strcmp(argv[1], "CFG") == 0) {
            respond_cfg();
            return;
        }

        if (strcmp(argv[1], "SAG") == 0) {
            respond_sag_status();
            return;
        }

        if (strcmp(argv[1], "LOG") == 0) {
            sag_event_t ev;
            long idx;
            if (argc < 3U) {
                respond_err("BAD_ARG");
                return;
            }
            if (strcmp(argv[2], "COUNT") == 0) {
                respond_log_count();
                return;
            }
            if (strcmp(argv[2], "LAST") == 0) {
                if (!log_get_last(&ev)) {
                    respond_err("EMPTY");
                    return;
                }
                respond_event(&ev, "LOG_LAST");
                return;
            }
            if (strcmp(argv[2], "IDX") == 0) {
                if (argc < 4U) {
                    respond_err("BAD_ARG");
                    return;
                }
                if (!parse_long(argv[3], &idx)) {
                    respond_err("BAD_ARG");
                    return;
                }
                if (idx < 0L || idx > 255L) {
                    respond_err("BAD_RANGE");
                    return;
                }
                if (!log_get_by_index((uint8_t)idx, &ev)) {
                    respond_err("BAD_INDEX");
                    return;
                }
                respond_event(&ev, "LOG_IDX");
                return;
            }
            respond_err("BAD_TARGET");
            return;
        }

        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "SET") == 0) {
        long v;
        uint16_t low;
        uint16_t recover;

        if (argc < 4U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "TH") != 0) {
            respond_err("BAD_TARGET");
            return;
        }

        if (!parse_long(argv[3], &v)) {
            respond_err("BAD_ARG");
            return;
        }
        if (v < 0L || v > 1023L) {
            respond_err("BAD_RANGE");
            return;
        }

        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            low = th_low;
            recover = th_recover;
        }

        if (strcmp(argv[2], "LOW") == 0) {
            low = (uint16_t)v;
        } else if (strcmp(argv[2], "RECOVER") == 0) {
            recover = (uint16_t)v;
        } else {
            respond_err("BAD_TARGET");
            return;
        }

        if (low >= recover) {
            respond_err("BAD_RANGE");
            return;
        }

        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            th_low = low;
            th_recover = recover;
        }
        respond_cfg();
        return;
    }

    if (strcmp(argv[0], "CLEAR") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }
        if (strcmp(argv[1], "LOG") == 0) {
            log_clear();
            uart_writeln("OK LOG_CLEARED=1");
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

    adc_init();
    uart_init(BAUD);
    timer1_init_1ms_tick();
    sei();

    uart_writeln("OK BOOT=P3");

    while (1) {
        cmd_line_result_t r;

        if (consume_sample_due()) {
            process_sample_tick();
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
