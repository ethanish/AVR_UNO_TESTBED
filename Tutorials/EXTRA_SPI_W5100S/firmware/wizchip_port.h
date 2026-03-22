#ifndef WIZCHIP_PORT_H
#define WIZCHIP_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <avr/io.h>

#ifndef NETCONFIG_SPI_CS_DDR
#define NETCONFIG_SPI_CS_DDR DDRB
#endif

#ifndef NETCONFIG_SPI_CS_PORT
#define NETCONFIG_SPI_CS_PORT PORTB
#endif

#ifndef NETCONFIG_SPI_CS_BIT
#define NETCONFIG_SPI_CS_BIT PB2
#endif

#ifndef NETCONFIG_SPI_MOSI_DDR
#define NETCONFIG_SPI_MOSI_DDR DDRB
#endif

#ifndef NETCONFIG_SPI_MOSI_BIT
#define NETCONFIG_SPI_MOSI_BIT PB3
#endif

#ifndef NETCONFIG_SPI_MISO_DDR
#define NETCONFIG_SPI_MISO_DDR DDRB
#endif

#ifndef NETCONFIG_SPI_MISO_BIT
#define NETCONFIG_SPI_MISO_BIT PB4
#endif

#ifndef NETCONFIG_SPI_SCK_DDR
#define NETCONFIG_SPI_SCK_DDR DDRB
#endif

#ifndef NETCONFIG_SPI_SCK_BIT
#define NETCONFIG_SPI_SCK_BIT PB5
#endif

#ifndef NETCONFIG_INT_DDR
#define NETCONFIG_INT_DDR DDRD
#endif

#ifndef NETCONFIG_INT_PORT
#define NETCONFIG_INT_PORT PORTD
#endif

#ifndef NETCONFIG_INT_BIT
#define NETCONFIG_INT_BIT PD2
#endif

void wizchip_port_interrupt_init(void);
void wizchip_port_enable_global_interrupts(void);
void wizchip_port_irq_set_pending(void);
void wizchip_port_irq_clear_pending(void);
uint8_t wizchip_port_irq_is_pending(void);

#ifdef __cplusplus
}
#endif

#endif
