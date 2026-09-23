/* A netif backed by memory instead of hardware.
 *
 * Tests inject synthetic frames with stub_link_inject() and inspect whatever
 * the stack transmitted with stub_link_sent(). This is what lets sequence
 * arithmetic, retransmission and FIN handling be exercised against cases
 * that would be impractical to reproduce on real hardware.
 */
#ifndef STUB_LINK_H
#define STUB_LINK_H

#include <stdint.h>
#include "../src/net/mn_netif.h"

#define STUB_QUEUE_DEPTH 32   /* a TCP exchange sends more than eight frames */

typedef struct {
    uint8_t  rx[STUB_QUEUE_DEPTH][MN_MAX_FRAME];
    uint16_t rx_len[STUB_QUEUE_DEPTH];
    uint8_t  rx_head, rx_count;

    uint8_t  tx[STUB_QUEUE_DEPTH][MN_MAX_FRAME];
    uint16_t tx_len[STUB_QUEUE_DEPTH];
    uint8_t  tx_count;

    uint8_t  tx_should_fail;    /* set to simulate a full hardware queue */
} stub_link;

void stub_link_init(stub_link *s, mn_netif *nif, const uint8_t *mac);

/* Queues a frame as though the hardware had received it. */
void stub_link_inject(stub_link *s, const uint8_t *frame, uint16_t len);

/* Returns the nth frame the stack transmitted, or NULL. */
const uint8_t *stub_link_sent(const stub_link *s, uint8_t n, uint16_t *len);

#endif
