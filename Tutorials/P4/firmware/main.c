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
#define CMD_BUF_SIZE 96

#define TIMER1_OCR1A_1MS 249U

#define CTRL_PERIOD_DEFAULT_MS 10U
#define KP_Q10_DEFAULT 1024
#define KI_Q10_DEFAULT 64
#define I_ACC_LIMIT 4096L
#define LPF_ALPHA_DEFAULT_PCT 25U
#define SETTLE_TOL_DEFAULT 4U
#define SETTLE_NEED_DEFAULT 20U

static volatile uint32_t tick_ms = 0;
static volatile bool control_due = false;
static volatile uint8_t control_period_ms = CTRL_PERIOD_DEFAULT_MS;
static volatile uint8_t control_accum_ms = 0;

static uint8_t target_pwm = 128U;
static uint8_t duty_pwm = 0U;
static uint8_t meas_pwm = 0U;

static bool mode_auto = true;
static int16_t kp_q10 = KP_Q10_DEFAULT;
static int16_t ki_q10 = KI_Q10_DEFAULT;
static int32_t i_acc = 0;
static uint8_t lpf_alpha_pct = LPF_ALPHA_DEFAULT_PCT;

static uint32_t control_count = 0;

static uint32_t resp_start_ms = 0;
static uint8_t resp_peak = 0;
static uint8_t resp_overshoot = 0;
static uint8_t resp_settle_tol = SETTLE_TOL_DEFAULT;
static uint8_t resp_settle_need = SETTLE_NEED_DEFAULT;
static uint8_t resp_settle_count = 0;
static bool resp_settled = false;
static uint32_t resp_settling_ms = 0;

static uint8_t clamp_u8(long v) {
    if (v < 0L) {
        return 0U;
    }
    if (v > 255L) {
        return 255U;
    }
    return (uint8_t)v;
}

static void timer1_init_1ms_tick(void) {
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
    OCR1A = TIMER1_OCR1A_1MS;
    TCNT1 = 0;
    TIMSK1 = _BV(OCIE1A);
}

static void pwm_init(void) {
    DDRB |= _BV(PB3);
    TCCR2A = _BV(COM2A1) | _BV(WGM21) | _BV(WGM20);
    TCCR2B = _BV(CS21);
    OCR2A = 0;
}

ISR(TIMER1_COMPA_vect) {
    uint8_t period = control_period_ms;
    tick_ms += 1U;
    control_accum_ms += 1U;
    if (control_accum_ms >= period) {
        control_accum_ms = 0U;
        control_due = true;
    }
}

static bool consume_control_due(void) {
    bool due;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        due = control_due;
        control_due = false;
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

static int parse_long(const char *s, long *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

static void apply_pwm(uint8_t duty) {
    duty_pwm = duty;
    OCR2A = duty;
}

static void respond_err(const char *e) {
    char msg[96];
    snprintf(msg, sizeof(msg), "ERR %s", e);
    uart_writeln(msg);
}

static void response_reset(void) {
    resp_start_ms = snapshot_tick_ms();
    resp_peak = meas_pwm;
    resp_overshoot = 0U;
    resp_settle_count = 0U;
    resp_settled = false;
    resp_settling_ms = 0U;
}

static void response_update(void) {
    uint8_t err_abs;

    if (meas_pwm > resp_peak) {
        resp_peak = meas_pwm;
    }
    if (resp_peak > target_pwm) {
        resp_overshoot = (uint8_t)(resp_peak - target_pwm);
    } else {
        resp_overshoot = 0U;
    }

    if (meas_pwm >= target_pwm) {
        err_abs = (uint8_t)(meas_pwm - target_pwm);
    } else {
        err_abs = (uint8_t)(target_pwm - meas_pwm);
    }

    if (err_abs <= resp_settle_tol) {
        if (resp_settle_count < 255U) {
            resp_settle_count += 1U;
        }
        if (!resp_settled && resp_settle_count >= resp_settle_need) {
            resp_settled = true;
            resp_settling_ms = snapshot_tick_ms() - resp_start_ms;
        }
    } else {
        resp_settle_count = 0U;
    }
}

static void update_measurement_model(void) {
    int16_t delta = (int16_t)duty_pwm - (int16_t)meas_pwm;
    int16_t adjust = (int16_t)(((int32_t)lpf_alpha_pct * delta) / 100L);

    if (adjust == 0 && delta != 0) {
        adjust = (delta > 0) ? 1 : -1;
    }
    meas_pwm = (uint8_t)((int16_t)meas_pwm + adjust);
}

static void control_step(void) {
    int16_t error;
    int32_t control_raw;
    int32_t i_next;
    uint8_t duty_cmd;

    update_measurement_model();

    if (mode_auto) {
        error = (int16_t)target_pwm - (int16_t)meas_pwm;
        i_next = i_acc + (int32_t)error;
        if (i_next > I_ACC_LIMIT) {
            i_next = I_ACC_LIMIT;
        }
        if (i_next < -I_ACC_LIMIT) {
            i_next = -I_ACC_LIMIT;
        }
        i_acc = i_next;

        control_raw = ((int32_t)kp_q10 * (int32_t)error) + ((int32_t)ki_q10 * i_acc);
        control_raw /= 1024L;
        duty_cmd = clamp_u8(control_raw);
        apply_pwm(duty_cmd);
    }

    control_count += 1U;
    response_update();
}

static void respond_pwm(void) {
    char msg[128];
    snprintf(msg,
             sizeof(msg),
             "OK MODE=%s TARGET=%u DUTY=%u MEAS=%u",
             mode_auto ? "AUTO" : "MANUAL",
             target_pwm,
             duty_pwm,
             meas_pwm);
    uart_writeln(msg);
}

static void respond_ctrl(void) {
    char msg[160];
    uint8_t period;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        period = control_period_ms;
    }

    snprintf(msg,
             sizeof(msg),
             "OK KP_Q10=%d KI_Q10=%d I_ACC=%ld LPF_PCT=%u PERIOD_MS=%u TOL=%u NEED=%u",
             kp_q10,
             ki_q10,
             (long)i_acc,
             lpf_alpha_pct,
             period,
             resp_settle_tol,
             resp_settle_need);
    uart_writeln(msg);
}

static void respond_resp(void) {
    char msg[160];
    snprintf(msg,
             sizeof(msg),
             "OK PEAK=%u OVERSHOOT=%u SETTLED=%u SETTLING_MS=%lu CTRL_COUNT=%lu",
             resp_peak,
             resp_overshoot,
             resp_settled ? 1U : 0U,
             (unsigned long)resp_settling_ms,
             (unsigned long)control_count);
    uart_writeln(msg);
}

static void respond_stat(void) {
    char msg[128];
    snprintf(msg,
             sizeof(msg),
             "OK UART_RX_ISR=%u TICK_MS=%lu CTRL_COUNT=%lu",
             uart_rx_isr_count_snapshot(),
             (unsigned long)snapshot_tick_ms(),
             (unsigned long)control_count);
    uart_writeln(msg);
}

static void handle_command(char *line) {
    char *argv[6] = {0};
    uint8_t argc = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok != NULL && argc < 6U) {
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
        uart_writeln("OK CMDS=PING,HELP,VERSION,GET,SET,RESET");
        return;
    }

    if (strcmp(argv[0], "VERSION") == 0) {
        uart_writeln("OK VERSION=P4-1.0");
        return;
    }

    if (strcmp(argv[0], "GET") == 0) {
        if (argc < 2U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "STAT") == 0) {
            respond_stat();
            return;
        }
        if (strcmp(argv[1], "PWM") == 0) {
            respond_pwm();
            return;
        }
        if (strcmp(argv[1], "CTRL") == 0) {
            respond_ctrl();
            return;
        }
        if (strcmp(argv[1], "RESP") == 0) {
            respond_resp();
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
        if (strcmp(argv[1], "RESP") == 0) {
            response_reset();
            uart_writeln("OK RESP_RESET=1");
            return;
        }
        if (strcmp(argv[1], "IACC") == 0) {
            i_acc = 0;
            uart_writeln("OK I_ACC=0");
            return;
        }
        respond_err("BAD_TARGET");
        return;
    }

    if (strcmp(argv[0], "SET") == 0) {
        long v;

        if (argc < 3U) {
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "TARGET") == 0) {
            if (!parse_long(argv[2], &v)) {
                respond_err("BAD_ARG");
                return;
            }
            if (v < 0L || v > 255L) {
                respond_err("BAD_RANGE");
                return;
            }
            target_pwm = (uint8_t)v;
            if (mode_auto) {
                response_reset();
            }
            respond_pwm();
            return;
        }

        if (strcmp(argv[1], "DUTY") == 0) {
            if (!parse_long(argv[2], &v)) {
                respond_err("BAD_ARG");
                return;
            }
            if (v < 0L || v > 255L) {
                respond_err("BAD_RANGE");
                return;
            }
            mode_auto = false;
            apply_pwm((uint8_t)v);
            response_reset();
            respond_pwm();
            return;
        }

        if (strcmp(argv[1], "MODE") == 0) {
            if (strcmp(argv[2], "AUTO") == 0) {
                mode_auto = true;
                response_reset();
                respond_pwm();
                return;
            }
            if (strcmp(argv[2], "MANUAL") == 0) {
                mode_auto = false;
                respond_pwm();
                return;
            }
            respond_err("BAD_ARG");
            return;
        }

        if (strcmp(argv[1], "GAIN") == 0) {
            if (argc < 4U || !parse_long(argv[3], &v)) {
                respond_err("BAD_ARG");
                return;
            }
            if (v < 0L || v > 8192L) {
                respond_err("BAD_RANGE");
                return;
            }
            if (strcmp(argv[2], "KP") == 0) {
                kp_q10 = (int16_t)v;
                respond_ctrl();
                return;
            }
            if (strcmp(argv[2], "KI") == 0) {
                ki_q10 = (int16_t)v;
                respond_ctrl();
                return;
            }
            respond_err("BAD_TARGET");
            return;
        }

        if (strcmp(argv[1], "LPF") == 0) {
            if (!parse_long(argv[2], &v)) {
                respond_err("BAD_ARG");
                return;
            }
            if (v < 1L || v > 100L) {
                respond_err("BAD_RANGE");
                return;
            }
            lpf_alpha_pct = (uint8_t)v;
            respond_ctrl();
            return;
        }

        if (strcmp(argv[1], "PERIOD") == 0) {
            if (!parse_long(argv[2], &v)) {
                respond_err("BAD_ARG");
                return;
            }
            if (v < 1L || v > 200L) {
                respond_err("BAD_RANGE");
                return;
            }
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                control_period_ms = (uint8_t)v;
                control_accum_ms = 0U;
                control_due = false;
            }
            respond_ctrl();
            return;
        }

        if (strcmp(argv[1], "SETTLE") == 0) {
            if (argc < 4U || !parse_long(argv[3], &v)) {
                respond_err("BAD_ARG");
                return;
            }
            if (strcmp(argv[2], "TOL") == 0) {
                if (v < 0L || v > 40L) {
                    respond_err("BAD_RANGE");
                    return;
                }
                resp_settle_tol = (uint8_t)v;
                respond_ctrl();
                return;
            }
            if (strcmp(argv[2], "NEED") == 0) {
                if (v < 1L || v > 200L) {
                    respond_err("BAD_RANGE");
                    return;
                }
                resp_settle_need = (uint8_t)v;
                respond_ctrl();
                return;
            }
            respond_err("BAD_TARGET");
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

    pwm_init();
    timer1_init_1ms_tick();
    uart_init(BAUD);
    response_reset();

    sei();
    uart_writeln("OK BOOT=P4");

    while (1) {
        cmd_line_result_t r;

        if (consume_control_due()) {
            control_step();
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
