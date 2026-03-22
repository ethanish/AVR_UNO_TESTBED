#ifndef NETCONFIG_H
#define NETCONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETCONFIG_OK 0
#define NETCONFIG_ERR_INIT -1
#define NETCONFIG_ERR_VERSION -2
#define NETCONFIG_ERR_ARG -3
#define NETCONFIG_EXPECTED_VERSION 0x51U
#define NETCONFIG_LINK_DOWN 0U
#define NETCONFIG_LINK_UP 1U
#define NETCONFIG_LOOPBACK_SOCKET 0U
#define NETCONFIG_LOOPBACK_PORT 5000U
#define NETCONFIG_SERVICE_NONE 0U
#define NETCONFIG_SERVICE_TCP_LOOPBACK 1U

typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
    uint8_t dns[4];
    uint8_t dhcp;
} netconfig_info_t;

typedef struct {
    uint8_t enabled;
    uint8_t service_type;
    uint8_t socket_num;
    uint8_t socket_state;
    uint16_t port;
    int32_t last_result;
} netconfig_service_status_t;

typedef netconfig_service_status_t netconfig_loopback_status_t;

extern const netconfig_info_t netconfig_default_info;

void netconfig_spi_init(void);
void netconfig_wizchip_if_init(void);
void netconfig_enable_global_interrupts(void);
int8_t netconfig_chip_init(const netconfig_info_t *netinfo);
void netconfig_cs_select(void);
void netconfig_cs_deselect(void);
uint8_t netconfig_spi_transfer(uint8_t tx);
uint32_t netconfig_spi_transfer_count(void);
uint8_t netconfig_get_version(void);
int8_t netconfig_get_link(uint8_t *link_state);
void netconfig_get_info(netconfig_info_t *netinfo);
uint8_t netconfig_reg_read(uint16_t addr);
void netconfig_reg_write(uint16_t addr, uint8_t value);
void netconfig_reg_read_buf(uint16_t addr, uint8_t *buf, uint8_t len);
int8_t netconfig_service_start(uint8_t sn, uint8_t service_type, uint16_t port);
void netconfig_service_stop(uint8_t sn);
void netconfig_poll(void);
void netconfig_get_status(uint8_t sn, netconfig_service_status_t *status);
int8_t netconfig_loopback_start(uint16_t port);
void netconfig_loopback_stop(void);
void netconfig_loopback_poll(void);
void netconfig_loopback_get_status(netconfig_loopback_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
