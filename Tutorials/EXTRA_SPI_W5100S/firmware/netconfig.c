#include "netconfig.h"
#include "wizchip_port.h"

#include <stdio.h>
#include <string.h>

#include "../../common/ioLibrary_Driver/Ethernet/wizchip_conf.h"
#include "../../common/ioLibrary_Driver/Ethernet/socket.h"

const netconfig_info_t netconfig_default_info = {
    .mac = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33},
    .ip = {192, 168, 77, 2},
    .sn = {255, 255, 255, 0},
    .gw = {192, 168, 77, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = NETINFO_STATIC,
};

static const uint8_t netconfig_tx_memsize[4] = {2, 2, 2, 2};
static const uint8_t netconfig_rx_memsize[4] = {2, 2, 2, 2};
static uint8_t netconfig_loopback_buf[DATA_BUF_SIZE];
static uint8_t netconfig_interrupt_ready = 0U;
static uint8_t netconfig_poll_cursor = 0U;

typedef struct {
    uint8_t enabled;
    uint8_t service_type;
    uint16_t port;
    int32_t last_result;
    uint16_t rx_len;
    uint16_t reply_len;
    uint16_t reply_offset;
    uint8_t rx_buf[DATA_BUF_SIZE];
    uint8_t reply_buf[DATA_BUF_SIZE];
} netconfig_socket_ctx_t;

static netconfig_socket_ctx_t netconfig_socket_ctx[_WIZCHIP_SOCK_NUM_];

#define NETCONFIG_TCP_SERVICE_MAX_RX 64U
#define NETCONFIG_TCP_SERVICE_MAX_TX 64U
#define NETCONFIG_TCP_SERVICE_MAX_STEPS 4U

static uint8_t netconfig_is_command(const uint8_t *buf, uint16_t len, const char *cmd) {
    uint16_t i;
    uint16_t cmd_len = (uint16_t)strlen(cmd);

    if (len < cmd_len || memcmp(buf, cmd, cmd_len) != 0) {
        return 0U;
    }

    for (i = cmd_len; i < len; i += 1U) {
        if (buf[i] != '\r' && buf[i] != '\n' && buf[i] != ' ' && buf[i] != '\t') {
            return 0U;
        }
    }

    return 1U;
}

static netconfig_socket_ctx_t *netconfig_get_socket_ctx(uint8_t sn) {
    if (sn >= _WIZCHIP_SOCK_NUM_) {
        return 0;
    }

    return &netconfig_socket_ctx[sn];
}

static void netconfig_reset_socket_ctx(uint8_t sn) {
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (ctx == 0) {
        return;
    }

    ctx->enabled = 0U;
    ctx->service_type = NETCONFIG_SERVICE_NONE;
    ctx->port = 0U;
    ctx->last_result = 0;
    ctx->rx_len = 0U;
    ctx->reply_len = 0U;
    ctx->reply_offset = 0U;
}

static void netconfig_clear_socket_runtime(netconfig_socket_ctx_t *ctx) {
    if (ctx == 0) {
        return;
    }

    ctx->rx_len = 0U;
    ctx->reply_len = 0U;
    ctx->reply_offset = 0U;
}

static void netconfig_reset_socket_reply(netconfig_socket_ctx_t *ctx) {
    if (ctx == 0) {
        return;
    }

    ctx->reply_len = 0U;
    ctx->reply_offset = 0U;
}

static uint16_t netconfig_prepare_reply(uint8_t *buf, uint16_t len, uint16_t port) {
    netconfig_info_t info;
    int written;

    if (netconfig_is_command(buf, len, "PING") != 0U) {
        memcpy(buf, "PONG\r\n", 6U);
        return 6U;
    }

    if (netconfig_is_command(buf, len, "NET TEST") != 0U) {
        netconfig_get_info(&info);
        written = snprintf((char *)buf,
                           DATA_BUF_SIZE,
                           "OK NET TEST IP=%u.%u.%u.%u PORT=%u\r\n",
                           info.ip[0],
                           info.ip[1],
                           info.ip[2],
                           info.ip[3],
                           port);
        if (written < 0) {
            return 0U;
        }
        if ((uint16_t)written > DATA_BUF_SIZE) {
            return DATA_BUF_SIZE;
        }
        return (uint16_t)written;
    }

    if ((uint16_t)(len + 2U) <= DATA_BUF_SIZE) {
        buf[len] = '\r';
        buf[len + 1U] = '\n';
        return (uint16_t)(len + 2U);
    }

    return len;
}

static void netconfig_compact_rx_buffer(netconfig_socket_ctx_t *ctx, uint16_t consumed_len) {
    if (ctx == 0 || consumed_len == 0U || consumed_len > ctx->rx_len) {
        return;
    }

    ctx->rx_len = (uint16_t)(ctx->rx_len - consumed_len);
    if (ctx->rx_len != 0U) {
        memmove(ctx->rx_buf, ctx->rx_buf + consumed_len, ctx->rx_len);
    }
}

static uint8_t netconfig_find_line_end(const netconfig_socket_ctx_t *ctx, uint16_t *line_len) {
    uint16_t i;

    if (ctx == 0 || line_len == 0) {
        return 0U;
    }

    for (i = 0U; i < ctx->rx_len; i += 1U) {
        if (ctx->rx_buf[i] == '\n') {
            *line_len = i;
            return 1U;
        }
    }

    return 0U;
}

static uint16_t netconfig_trim_line_len(const uint8_t *buf, uint16_t len) {
    while (len != 0U && (buf[len - 1U] == '\r' || buf[len - 1U] == '\n')) {
        len = (uint16_t)(len - 1U);
    }

    return len;
}

static uint16_t netconfig_extract_reply_from_rx(netconfig_socket_ctx_t *ctx) {
    uint16_t line_len;
    uint16_t raw_line_len;
    uint16_t reply_len;

    if (ctx == 0) {
        return 0U;
    }

    if (netconfig_find_line_end(ctx, &line_len) == 0U) {
        if (ctx->rx_len >= DATA_BUF_SIZE) {
            reply_len = netconfig_prepare_reply(ctx->rx_buf, ctx->rx_len, ctx->port);
            if (reply_len > 0U) {
                memcpy(ctx->reply_buf, ctx->rx_buf, reply_len);
            }
            ctx->rx_len = 0U;
            return reply_len;
        }
        return 0U;
    }

    raw_line_len = (uint16_t)(line_len + 1U);
    line_len = netconfig_trim_line_len(ctx->rx_buf, line_len);
    reply_len = netconfig_prepare_reply(ctx->rx_buf, line_len, ctx->port);
    if (reply_len > 0U) {
        memcpy(ctx->reply_buf, ctx->rx_buf, reply_len);
    }

    netconfig_compact_rx_buffer(ctx, raw_line_len);
    return reply_len;
}

static uint16_t netconfig_queue_reply_from_rx(uint8_t sn) {
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (ctx == 0 || ctx->reply_offset < ctx->reply_len) {
        return 0U;
    }

    ctx->reply_len = netconfig_extract_reply_from_rx(ctx);
    ctx->reply_offset = 0U;
    return ctx->reply_len;
}

static int32_t netconfig_service_tcp_server(uint8_t sn) {
    int32_t ret;
    uint16_t recv_size;
    uint16_t send_size;
    uint8_t socket_state;
    uint8_t steps;
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (ctx == 0) {
        return NETCONFIG_ERR_ARG;
    }

    socket_state = getSn_SR(sn);

    switch (socket_state) {
    case SOCK_ESTABLISHED:
    case SOCK_CLOSE_WAIT:
        if ((getSn_IR(sn) & Sn_IR_CON) != 0U) {
            setSn_IR(sn, Sn_IR_CON);
        }

        for (steps = 0U; steps < NETCONFIG_TCP_SERVICE_MAX_STEPS; steps += 1U) {
            if (ctx->reply_offset < ctx->reply_len) {
                send_size = (uint16_t)(ctx->reply_len - ctx->reply_offset);
                if (send_size > NETCONFIG_TCP_SERVICE_MAX_TX) {
                    send_size = NETCONFIG_TCP_SERVICE_MAX_TX;
                }

                ret = send(sn, ctx->reply_buf + ctx->reply_offset, send_size);
                if (ret < 0) {
                    netconfig_clear_socket_runtime(ctx);
                    close(sn);
                    return ret;
                }

                ctx->reply_offset = (uint16_t)(ctx->reply_offset + (uint16_t)ret);
                if (ctx->reply_offset >= ctx->reply_len) {
                    netconfig_reset_socket_reply(ctx);
                    (void)netconfig_queue_reply_from_rx(sn);
                }
                continue;
            }

            if (netconfig_queue_reply_from_rx(sn) > 0U) {
                continue;
            }

            recv_size = getSn_RX_RSR(sn);
            if (recv_size != 0U) {
                if (recv_size > NETCONFIG_TCP_SERVICE_MAX_RX) {
                    recv_size = NETCONFIG_TCP_SERVICE_MAX_RX;
                }

                ret = recv(sn, netconfig_loopback_buf, recv_size);
                if (ret <= 0) {
                    return ret;
                }

                if ((uint16_t)ret > (uint16_t)(DATA_BUF_SIZE - ctx->rx_len)) {
                    ctx->rx_len = 0U;
                }

                memcpy(ctx->rx_buf + ctx->rx_len, netconfig_loopback_buf, (uint16_t)ret);
                ctx->rx_len = (uint16_t)(ctx->rx_len + (uint16_t)ret);

                send_size = netconfig_queue_reply_from_rx(sn);
                if (send_size == 0U && ctx->rx_len >= DATA_BUF_SIZE) {
                    ctx->rx_len = 0U;
                }
                continue;
            }

            break;
        }

        if (socket_state == SOCK_CLOSE_WAIT) {
            if (ctx->reply_len == 0U && ctx->rx_len == 0U) {
                netconfig_clear_socket_runtime(ctx);
                ret = disconnect(sn);
                return ret;
            }
        }

        return 1;

    case SOCK_INIT:
        ret = listen(sn);
        return ret;

    case SOCK_CLOSED:
        netconfig_clear_socket_runtime(ctx);
        ret = socket(sn, Sn_MR_TCP, ctx->port, 0x00);
        return ret;

    default:
        return 1;
    }
}

static uint8_t netconfig_service_interrupt_mask(void) {
    return (uint8_t)(Sn_IR_CON | Sn_IR_RECV | Sn_IR_DISCON | Sn_IR_TIMEOUT);
}

static void netconfig_refresh_interrupt_masks(void) {
    uint8_t sn;
    uint8_t global_mask = 0U;
    netconfig_socket_ctx_t *ctx;

    for (sn = 0U; sn < _WIZCHIP_SOCK_NUM_; sn += 1U) {
        ctx = netconfig_get_socket_ctx(sn);
        if (ctx != 0 && ctx->enabled != 0U) {
            setSn_IMR(sn, netconfig_service_interrupt_mask());
            setSn_IR(sn, netconfig_service_interrupt_mask());
            global_mask |= IMR_SOCK(sn);
        } else {
            setSn_IMR(sn, 0x00U);
        }
    }

    setIMR(global_mask);
    if (global_mask != 0U) {
        setIR(global_mask);
    }
}

static uint8_t netconfig_socket_needs_service(uint8_t sn) {
    uint8_t socket_state;
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (ctx == 0 || ctx->enabled == 0U) {
        return 0U;
    }

    socket_state = getSn_SR(sn);
    if (socket_state == SOCK_CLOSED || socket_state == SOCK_INIT || socket_state == SOCK_CLOSE_WAIT) {
        return 1U;
    }

    if (wizchip_port_irq_is_pending() != 0U) {
        return 1U;
    }

    if ((getIR() & IR_SOCK(sn)) != 0U) {
        return 1U;
    }

    if ((getSn_IR(sn) & netconfig_service_interrupt_mask()) != 0U) {
        return 1U;
    }

    return 0U;
}

static uint8_t netconfig_find_service_socket(uint8_t *sn_out) {
    uint8_t i;
    uint8_t sn;

    if (sn_out == 0) {
        return 0U;
    }

    for (i = 0U; i < _WIZCHIP_SOCK_NUM_; i += 1U) {
        sn = (uint8_t)((netconfig_poll_cursor + i) % _WIZCHIP_SOCK_NUM_);
        if (netconfig_socket_needs_service(sn) != 0U) {
            *sn_out = sn;
            netconfig_poll_cursor = (uint8_t)((sn + 1U) % _WIZCHIP_SOCK_NUM_);
            return 1U;
        }
    }

    return 0U;
}

static void netconfig_copy_from_wiznet(netconfig_info_t *dst, const wiz_NetInfo *src) {
    memcpy(dst->mac, src->mac, sizeof(dst->mac));
    memcpy(dst->ip, src->ip, sizeof(dst->ip));
    memcpy(dst->sn, src->sn, sizeof(dst->sn));
    memcpy(dst->gw, src->gw, sizeof(dst->gw));
    memcpy(dst->dns, src->dns, sizeof(dst->dns));
    dst->dhcp = src->dhcp;
}

static void netconfig_copy_to_wiznet(wiz_NetInfo *dst, const netconfig_info_t *src) {
    memcpy(dst->mac, src->mac, sizeof(dst->mac));
    memcpy(dst->ip, src->ip, sizeof(dst->ip));
    memcpy(dst->sn, src->sn, sizeof(dst->sn));
    memcpy(dst->gw, src->gw, sizeof(dst->gw));
    memcpy(dst->dns, src->dns, sizeof(dst->dns));
    dst->dhcp = src->dhcp;
}

uint8_t netconfig_get_version(void) {
    return getVER();
}

int8_t netconfig_get_link(uint8_t *link_state) {
    if (link_state == 0) {
        return -1;
    }

    return ctlwizchip(CW_GET_PHYLINK, link_state);
}

void netconfig_get_info(netconfig_info_t *netinfo) {
    wiz_NetInfo wiznet_info;

    if (netinfo == 0) {
        return;
    }

    ctlnetwork(CN_GET_NETINFO, &wiznet_info);
    netconfig_copy_from_wiznet(netinfo, &wiznet_info);
}

uint8_t netconfig_reg_read(uint16_t addr) {
    return WIZCHIP_READ(addr);
}

void netconfig_reg_write(uint16_t addr, uint8_t value) {
    WIZCHIP_WRITE(addr, value);
}

void netconfig_reg_read_buf(uint16_t addr, uint8_t *buf, uint8_t len) {
    WIZCHIP_READ_BUF(addr, buf, len);
}

void netconfig_enable_global_interrupts(void) {
    wizchip_port_enable_global_interrupts();
}

int8_t netconfig_loopback_start(uint16_t port) {
    return netconfig_service_start(NETCONFIG_LOOPBACK_SOCKET, NETCONFIG_SERVICE_TCP_LOOPBACK, port);
}

void netconfig_loopback_stop(void) {
    netconfig_service_stop(NETCONFIG_LOOPBACK_SOCKET);
}

int8_t netconfig_service_start(uint8_t sn, uint8_t service_type, uint16_t port) {
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (ctx == 0 || port == 0U || service_type == NETCONFIG_SERVICE_NONE) {
        return NETCONFIG_ERR_ARG;
    }

    netconfig_reset_socket_ctx(sn);
    ctx->enabled = 1U;
    ctx->service_type = service_type;
    ctx->port = port;
    ctx->last_result = 1;

    if (netconfig_interrupt_ready == 0U) {
        wizchip_port_interrupt_init();
        netconfig_interrupt_ready = 1U;
    }

    wizchip_port_irq_set_pending();
    netconfig_refresh_interrupt_masks();
    close(sn);
    return NETCONFIG_OK;
}

void netconfig_service_stop(uint8_t sn) {
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (ctx == 0) {
        return;
    }

    wizchip_port_irq_clear_pending();
    netconfig_reset_socket_ctx(sn);
    netconfig_refresh_interrupt_masks();
    close(sn);
}

void netconfig_poll(void) {
    uint8_t sn;
    uint8_t socket_ir;
    uint8_t global_ir;
    netconfig_socket_ctx_t *ctx;

    if (netconfig_find_service_socket(&sn) == 0U) {
        return;
    }

    ctx = netconfig_get_socket_ctx(sn);
    if (ctx == 0) {
        return;
    }

    wizchip_port_irq_clear_pending();
    if (ctx->service_type == NETCONFIG_SERVICE_TCP_LOOPBACK) {
        ctx->last_result = netconfig_service_tcp_server(sn);
    }

    socket_ir = getSn_IR(sn);
    if (socket_ir != 0U) {
        setSn_IR(sn, socket_ir);
    }

    global_ir = getIR();
    if ((global_ir & IR_SOCK(sn)) != 0U) {
        setIR(IR_SOCK(sn));
    }
}

void netconfig_get_status(uint8_t sn, netconfig_service_status_t *status) {
    netconfig_socket_ctx_t *ctx = netconfig_get_socket_ctx(sn);

    if (status == 0 || ctx == 0) {
        return;
    }

    status->enabled = ctx->enabled;
    status->service_type = ctx->service_type;
    status->socket_num = sn;
    status->socket_state = getSn_SR(sn);
    status->port = ctx->port;
    status->last_result = ctx->last_result;
}

void netconfig_loopback_poll(void) {
    netconfig_poll();
}

void netconfig_loopback_get_status(netconfig_loopback_status_t *status) {
    netconfig_get_status(NETCONFIG_LOOPBACK_SOCKET, status);
}

int8_t netconfig_chip_init(const netconfig_info_t *netinfo) {
    wiz_NetInfo applied_netinfo;

    netconfig_spi_init();
    netconfig_wizchip_if_init();

    if (wizchip_init((uint8_t *)netconfig_tx_memsize, (uint8_t *)netconfig_rx_memsize) != 0) {
        return NETCONFIG_ERR_INIT;
    }

    if (netconfig_get_version() != NETCONFIG_EXPECTED_VERSION) {
        return NETCONFIG_ERR_VERSION;
    }

    if (netinfo != 0) {
        netconfig_copy_to_wiznet(&applied_netinfo, netinfo);
    } else {
        netconfig_copy_to_wiznet(&applied_netinfo, &netconfig_default_info);
    }

    (void)ctlnetwork(CN_SET_NETINFO, &applied_netinfo);
    return NETCONFIG_OK;
}
