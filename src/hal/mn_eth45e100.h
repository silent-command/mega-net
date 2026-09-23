/* The 45E100 Ethernet controller as an mn_netif. Target only. */
#ifndef MN_ETH45E100_H
#define MN_ETH45E100_H

#include "../net/mn_netif.h"

/* Brings the controller up and fills in nif. Reads the MAC from the
 * hardware. Takes about four seconds: the PHY needs that after reset. */
void mn_eth45e100_init(mn_netif *nif);

/* Raw register reads, for diagnostics. */
uint8_t mn_eth45e100_ctrl1(void);
uint8_t mn_eth45e100_ctrl2(void);

#endif
