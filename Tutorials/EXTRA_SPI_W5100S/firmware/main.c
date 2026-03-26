#define F_CPU 16000000UL

#include <stdint.h>

#include "netconfig.h"

int main(void) {
    int8_t netconfig_status;

    netconfig_status = netconfig_chip_init(0);
    netconfig_enable_global_interrupts();

    if (netconfig_status == NETCONFIG_OK) {
        (void)netconfig_start_default_services();
    }

    while (1) {
        netconfig_poll();
    }
}
