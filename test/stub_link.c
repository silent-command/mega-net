#include "stub_link.h"
#include <string.h>

static uint8_t stub_rx(mn_netif *nif, uint8_t *buf, uint16_t cap,
                       uint16_t *len)
{
    stub_link *s = (stub_link *)nif->ctx;
    uint16_t n;

    if (s->rx_count == 0)
        return 0;

    n = s->rx_len[s->rx_head];
    if (n > cap)
        n = cap;
    memcpy(buf, s->rx[s->rx_head], n);
    *len = n;

    s->rx_head = (uint8_t)((s->rx_head + 1) % STUB_QUEUE_DEPTH);
    s->rx_count--;
    return 1;
}

static uint8_t stub_tx(mn_netif *nif, const uint8_t *buf, uint16_t len)
{
    stub_link *s = (stub_link *)nif->ctx;

    if (s->tx_should_fail)
        return 0;
    if (s->tx_count >= STUB_QUEUE_DEPTH)
        return 0;
    if (len > MN_MAX_FRAME)
        return 0;

    memcpy(s->tx[s->tx_count], buf, len);
    s->tx_len[s->tx_count] = len;
    s->tx_count++;
    return 1;
}

void stub_link_init(stub_link *s, mn_netif *nif, const uint8_t *mac)
{
    memset(s, 0, sizeof *s);
    memset(nif, 0, sizeof *nif);
    memcpy(nif->mac, mac, MN_ETH_ADDR_LEN);
    nif->rx = stub_rx;
    nif->tx = stub_tx;
    nif->ctx = s;
}

void stub_link_inject(stub_link *s, const uint8_t *frame, uint16_t len)
{
    uint8_t slot;

    if (s->rx_count >= STUB_QUEUE_DEPTH)
        return;
    if (len > MN_MAX_FRAME)
        len = MN_MAX_FRAME;

    slot = (uint8_t)((s->rx_head + s->rx_count) % STUB_QUEUE_DEPTH);
    memcpy(s->rx[slot], frame, len);
    s->rx_len[slot] = len;
    s->rx_count++;
}

const uint8_t *stub_link_sent(const stub_link *s, uint8_t n, uint16_t *len)
{
    if (n >= s->tx_count)
        return NULL;
    if (len)
        *len = s->tx_len[n];
    return s->tx[n];
}
