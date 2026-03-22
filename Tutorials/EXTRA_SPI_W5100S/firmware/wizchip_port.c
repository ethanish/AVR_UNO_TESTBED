#include "wizchip_port.h"
#include "netconfig.h"

#include <avr/interrupt.h>

#include "../../common/ioLibrary_Driver/Ethernet/wizchip_conf.h"

static uint8_t wizchip_port_sreg = 0U;
static volatile uint32_t wizchip_port_spi_xfer_count = 0U;
static volatile uint8_t wizchip_port_irq_pending = 0U;

static void wizchip_port_cris_enter(void) {
    wizchip_port_sreg = SREG;
    cli();
}

static void wizchip_port_cris_exit(void) {
    SREG = wizchip_port_sreg;
}

static uint8_t wizchip_port_spi_readbyte(void) {
    return netconfig_spi_transfer(0x00U);
}

static void wizchip_port_spi_writebyte(uint8_t wb) {
    (void)netconfig_spi_transfer(wb);
}

static void wizchip_port_spi_readburst(uint8_t *buf, uint16_t len) {
    uint16_t i;

    for (i = 0U; i < len; i += 1U) {
        buf[i] = wizchip_port_spi_readbyte();
    }
}

static void wizchip_port_spi_writeburst(uint8_t *buf, uint16_t len) {
    uint16_t i;

    for (i = 0U; i < len; i += 1U) {
        wizchip_port_spi_writebyte(buf[i]);
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
    SPSR = 0U;
}

void netconfig_wizchip_if_init(void) {
    reg_wizchip_cris_cbfunc(wizchip_port_cris_enter, wizchip_port_cris_exit);
    reg_wizchip_cs_cbfunc(netconfig_cs_select, netconfig_cs_deselect);
    reg_wizchip_spi_cbfunc(wizchip_port_spi_readbyte, wizchip_port_spi_writebyte);
    reg_wizchip_spiburst_cbfunc(wizchip_port_spi_readburst, wizchip_port_spi_writeburst);
}

uint8_t netconfig_spi_transfer(uint8_t tx) {
    SPDR = tx;
    while ((SPSR & (uint8_t)(1U << SPIF)) == 0U) {
    }

    wizchip_port_spi_xfer_count += 1U;
    return SPDR;
}

uint32_t netconfig_spi_transfer_count(void) {
    return wizchip_port_spi_xfer_count;
}

void wizchip_port_interrupt_init(void) {
    NETCONFIG_INT_DDR &= (uint8_t)~(uint8_t)(1U << NETCONFIG_INT_BIT);
    NETCONFIG_INT_PORT |= (uint8_t)(1U << NETCONFIG_INT_BIT);
    EICRA = (uint8_t)((EICRA & (uint8_t)~0x03U) | 0x02U);
    EIFR = (uint8_t)(1U << INTF0);
    EIMSK |= (uint8_t)(1U << INT0);
}

void wizchip_port_enable_global_interrupts(void) {
    sei();
}

void wizchip_port_irq_set_pending(void) {
    wizchip_port_irq_pending = 1U;
}

void wizchip_port_irq_clear_pending(void) {
    wizchip_port_irq_pending = 0U;
}

uint8_t wizchip_port_irq_is_pending(void) {
    return wizchip_port_irq_pending;
}

ISR(INT0_vect) {
    wizchip_port_irq_set_pending();
}
