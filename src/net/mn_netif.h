/* The link layer boundary.
 *
 * This is the whole of the hardware interface. Everything above it is
 * portable C that compiles and runs natively on the host; everything below
 * it is either the 45E100 driver (target) or the stub (tests). Keeping this
 * seam narrow is what makes the stack testable in milliseconds instead of
 * through a deploy-and-observe loop.
 */
#ifndef MN_NETIF_H
#define MN_NETIF_H

#include <stdint.h>

#define MN_ETH_ADDR_LEN 6
#define MN_MAX_FRAME    1518    /* 1500 MTU + 14 header + 4 FCS */

struct mn_netif;

typedef struct mn_netif {
    uint8_t mac[MN_ETH_ADDR_LEN];

    /* Pulls one frame if the hardware has one waiting. Returns 1 and sets
     * *len when a frame was copied into buf, 0 when nothing is pending.
     * Never blocks: polling is the caller's business, and every wait in
     * this stack is bounded. */
    uint8_t (*rx)(struct mn_netif *nif, uint8_t *buf, uint16_t cap,
                  uint16_t *len);

    /* Queues one frame for transmission. Returns 1 on success. */
    uint8_t (*tx)(struct mn_netif *nif, const uint8_t *buf, uint16_t len);

    void *ctx;                  /* driver private state */
} mn_netif;

#endif
