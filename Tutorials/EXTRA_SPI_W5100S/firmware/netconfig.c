#include "netconfig.h"

#include <avr/interrupt.h>
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

static uint8_t netconfig_sreg = 0U;
static volatile uint32_t netconfig_spi_xfer_count = 0U;
static const uint8_t netconfig_tx_memsize[4] = {2, 2, 2, 2};
static const uint8_t netconfig_rx_memsize[4] = {2, 2, 2, 2};
static uint8_t netconfig_loopback_buf[DATA_BUF_SIZE];
static uint8_t netconfig_loopback_enabled = 0U;
static uint16_t netconfig_loopback_port = NETCONFIG_LOOPBACK_PORT;
static int32_t netconfig_loopback_last_result = 0;
static uint8_t netconfig_interrupt_ready = 0U;
static volatile uint8_t netconfig_wiznet_irq_pending = 0U;

typedef struct {
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

static uint16_t netconfig_prepare_reply(uint8_t *buf, uint16_t len) {
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
                           netconfig_loopback_port);
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
            reply_len = netconfig_prepare_reply(ctx->rx_buf, ctx->rx_len);
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
    reply_len = netconfig_prepare_reply(ctx->rx_buf, line_len);
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

static int32_t netconfig_service_tcp_server(uint8_t sn, uint16_t port) {
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
                    netconfig_reset_socket_ctx(sn);
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
                netconfig_reset_socket_ctx(sn);
                ret = disconnect(sn);
                return ret;
            }
        }

        return 1;

    case SOCK_INIT:
        ret = listen(sn);
        return ret;

    case SOCK_CLOSED:
        netconfig_reset_socket_ctx(sn);
        ret = socket(sn, Sn_MR_TCP, port, 0x00);
        return ret;

    default:
        return 1;
    }
}

static void netconfig_interrupt_init(void) {
    NETCONFIG_INT_DDR &= (uint8_t)~(uint8_t)(1U << NETCONFIG_INT_BIT);
    NETCONFIG_INT_PORT |= (uint8_t)(1U << NETCONFIG_INT_BIT);
    EICRA = (uint8_t)((EICRA & (uint8_t)~0x03U) | 0x02U);
    EIFR = (uint8_t)(1U << INTF0);
    EIMSK |= (uint8_t)(1U << INT0);
}

static void netconfig_loopback_interrupt_enable(void) {
    setIMR(IMR_SOCK(NETCONFIG_LOOPBACK_SOCKET));
    setSn_IMR(NETCONFIG_LOOPBACK_SOCKET,
              (uint8_t)(Sn_IR_CON | Sn_IR_RECV | Sn_IR_DISCON | Sn_IR_TIMEOUT));
    setIR(IR_SOCK(NETCONFIG_LOOPBACK_SOCKET));
    setSn_IR(NETCONFIG_LOOPBACK_SOCKET,
             (uint8_t)(Sn_IR_CON | Sn_IR_RECV | Sn_IR_DISCON | Sn_IR_TIMEOUT));
}

static void netconfig_loopback_interrupt_disable(void) {
    setSn_IMR(NETCONFIG_LOOPBACK_SOCKET, 0x00U);
    setIMR(0x00U);
    setIR(IR_SOCK(NETCONFIG_LOOPBACK_SOCKET));
    setSn_IR(NETCONFIG_LOOPBACK_SOCKET,
             (uint8_t)(Sn_IR_CON | Sn_IR_RECV | Sn_IR_DISCON | Sn_IR_TIMEOUT));
}

static uint8_t netconfig_loopback_needs_service(void) {
    uint8_t socket_state;

    if (netconfig_loopback_enabled == 0U) {
        return 0U;
    }

    socket_state = getSn_SR(NETCONFIG_LOOPBACK_SOCKET);
    if (socket_state == SOCK_CLOSED || socket_state == SOCK_INIT || socket_state == SOCK_CLOSE_WAIT) {
        return 1U;
    }

    if (netconfig_wiznet_irq_pending != 0U) {
        return 1U;
    }

    if ((getIR() & IR_SOCK(NETCONFIG_LOOPBACK_SOCKET)) != 0U) {
        return 1U;
    }

    if ((getSn_IR(NETCONFIG_LOOPBACK_SOCKET) &
         (uint8_t)(Sn_IR_CON | Sn_IR_RECV | Sn_IR_DISCON | Sn_IR_TIMEOUT)) != 0U) {
        return 1U;
    }

    return 0U;
}

ISR(INT0_vect) {
    netconfig_wiznet_irq_pending = 1U;
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

static void netconfig_cris_enter(void) {
    netconfig_sreg = SREG;
    cli();
}

static void netconfig_cris_exit(void) {
    SREG = netconfig_sreg;
}

static uint8_t netconfig_spi_readbyte(void) {
    return netconfig_spi_transfer(0x00U);
}

static void netconfig_spi_writebyte(uint8_t wb) {
    (void)netconfig_spi_transfer(wb);
}

static void netconfig_spi_readburst(uint8_t *buf, uint16_t len) {
    uint16_t i;

    for (i = 0; i < len; i += 1U) {
        buf[i] = netconfig_spi_readbyte();
    }
}

static void netconfig_spi_writeburst(uint8_t *buf, uint16_t len) {
    uint16_t i;

    for (i = 0; i < len; i += 1U) {
        netconfig_spi_writebyte(buf[i]);
    }
}

void netconfig_cs_select(void) {
    NETCONFIG_SPI_CS_PORT &= (uint8_t)~(uint8_t)(1U << NETCONFIG_SPI_CS_BIT);
}

void netconfig_cs_deselect(void) {
    NETCONFIG_SPI_CS_PORT |= (uint8_t)(1U << NETCONFIG_SPI_CS_BIT);
}

void netconfig_spi_init(void) {
    NETCONFIG_SPI_CS_DDR |= (uint8_t)(1U << NETCONFIG_SPI_CS_BIT);
    NETCONFIG_SPI_MOSI_DDR |= (uint8_t)(1U << NETCONFIG_SPI_MOSI_BIT);
    NETCONFIG_SPI_SCK_DDR |= (uint8_t)(1U << NETCONFIG_SPI_SCK_BIT);
    NETCONFIG_SPI_MISO_DDR &= (uint8_t)~(uint8_t)(1U << NETCONFIG_SPI_MISO_BIT);

    netconfig_cs_deselect();

    SPCR = (uint8_t)((1U << SPE) | (1U << MSTR) | (1U << SPR0));
    SPSR = 0;
}

void netconfig_wizchip_if_init(void) {
    reg_wizchip_cris_cbfunc(netconfig_cris_enter, netconfig_cris_exit);
    reg_wizchip_cs_cbfunc(netconfig_cs_select, netconfig_cs_deselect);
    reg_wizchip_spi_cbfunc(netconfig_spi_readbyte, netconfig_spi_writebyte);
    reg_wizchip_spiburst_cbfunc(netconfig_spi_readburst, netconfig_spi_writeburst);
}

uint8_t netconfig_spi_transfer(uint8_t tx) {
    SPDR = tx;
    while ((SPSR & (uint8_t)(1U << SPIF)) == 0U) {
    }
    netconfig_spi_xfer_count += 1U;
    return SPDR;
}

uint32_t netconfig_spi_transfer_count(void) {
    return netconfig_spi_xfer_count;
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

int8_t netconfig_loopback_start(uint16_t port) {
    if (port == 0U) {
        return NETCONFIG_ERR_ARG;
    }

    netconfig_loopback_port = port;
    netconfig_loopback_enabled = 1U;
    netconfig_loopback_last_result = 1;
    netconfig_reset_socket_ctx(NETCONFIG_LOOPBACK_SOCKET);

    if (netconfig_interrupt_ready == 0U) {
        netconfig_interrupt_init();
        netconfig_interrupt_ready = 1U;
    }
    netconfig_wiznet_irq_pending = 1U;
    netconfig_loopback_interrupt_enable();

    close(NETCONFIG_LOOPBACK_SOCKET);
    return NETCONFIG_OK;
}

void netconfig_loopback_stop(void) {
    netconfig_loopback_enabled = 0U;
    netconfig_loopback_last_result = 0;
    netconfig_wiznet_irq_pending = 0U;
    netconfig_reset_socket_ctx(NETCONFIG_LOOPBACK_SOCKET);
    if (netconfig_interrupt_ready != 0U) {
        netconfig_loopback_interrupt_disable();
    }
    close(NETCONFIG_LOOPBACK_SOCKET);
}

void netconfig_loopback_poll(void) {
    uint8_t socket_ir;
    uint8_t global_ir;

    if (netconfig_loopback_needs_service() == 0U) {
        return;
    }

    netconfig_wiznet_irq_pending = 0U;
    netconfig_loopback_last_result =
        netconfig_service_tcp_server(NETCONFIG_LOOPBACK_SOCKET, netconfig_loopback_port);

    socket_ir = getSn_IR(NETCONFIG_LOOPBACK_SOCKET);
    if (socket_ir != 0U) {
        setSn_IR(NETCONFIG_LOOPBACK_SOCKET, socket_ir);
    }

    global_ir = getIR();
    if ((global_ir & IR_SOCK(NETCONFIG_LOOPBACK_SOCKET)) != 0U) {
        setIR(IR_SOCK(NETCONFIG_LOOPBACK_SOCKET));
    }
}

void netconfig_loopback_get_status(netconfig_loopback_status_t *status) {
    if (status == 0) {
        return;
    }

    status->enabled = netconfig_loopback_enabled;
    status->socket_num = NETCONFIG_LOOPBACK_SOCKET;
    status->socket_state = getSn_SR(NETCONFIG_LOOPBACK_SOCKET);
    status->port = netconfig_loopback_port;
    status->last_result = netconfig_loopback_last_result;
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
