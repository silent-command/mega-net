#include "mn_arp.h"
#include "mn_ether.h"
#include "mn_byteorder.h"

/* ARP payload offsets, from the start of the Ethernet frame. */
#define OFF_HTYPE  14
#define OFF_PTYPE  16
#define OFF_HLEN   18
#define OFF_PLEN   19
#define OFF_OPER   20
#define OFF_SHA    22   /* sender hardware address */
#define OFF_SPA    28   /* sender protocol address */
#define OFF_THA    32   /* target hardware address */
#define OFF_TPA    38   /* target protocol address */

#define HTYPE_ETHERNET 1

static void copy(uint8_t *dst, const uint8_t *src, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static uint8_t eq(const uint8_t *a, const uint8_t *b, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* The fixed part every ARP-over-Ethernet-for-IPv4 packet shares. */
static void fixed_header(uint8_t *buf, uint16_t oper)
{
    mn_put16(buf + OFF_HTYPE, HTYPE_ETHERNET);
    mn_put16(buf + OFF_PTYPE, MN_ETHERTYPE_IPV4);
    buf[OFF_HLEN] = MN_ETH_ADDR_LEN;
    buf[OFF_PLEN] = MN_IPV4_ADDR_LEN;
    mn_put16(buf + OFF_OPER, oper);
}

/* 1 if the frame is long enough and is ARP for IPv4 over Ethernet. */
static uint8_t is_arp(const uint8_t *frame, uint16_t len)
{
    if (len < MN_ARP_FRAME_LEN)
        return 0;
    if (mn_ether_type(frame) != MN_ETHERTYPE_ARP)
        return 0;
    if (mn_get16(frame + OFF_HTYPE) != HTYPE_ETHERNET)
        return 0;
    if (mn_get16(frame + OFF_PTYPE) != MN_ETHERTYPE_IPV4)
        return 0;
    if (frame[OFF_HLEN] != MN_ETH_ADDR_LEN || frame[OFF_PLEN] != MN_IPV4_ADDR_LEN)
        return 0;
    return 1;
}

uint16_t mn_arp_build_request(uint8_t *buf, const uint8_t *sender_mac,
                              const uint8_t *sender_ip,
                              const uint8_t *target_ip)
{
    uint8_t i;

    mn_ether_build(buf, mn_eth_broadcast, sender_mac, MN_ETHERTYPE_ARP);
    fixed_header(buf, MN_ARP_OP_REQUEST);
    copy(buf + OFF_SHA, sender_mac, MN_ETH_ADDR_LEN);
    copy(buf + OFF_SPA, sender_ip, MN_IPV4_ADDR_LEN);
    for (i = 0; i < MN_ETH_ADDR_LEN; i++)
        buf[OFF_THA + i] = 0;               /* unknown: that is the question */
    copy(buf + OFF_TPA, target_ip, MN_IPV4_ADDR_LEN);
    return MN_ARP_FRAME_LEN;
}

uint16_t mn_arp_build_reply(uint8_t *buf, const uint8_t *request,
                            const uint8_t *our_mac, const uint8_t *our_ip)
{
    /* Reply goes back to whoever asked, unicast. */
    mn_ether_build(buf, request + OFF_SHA, our_mac, MN_ETHERTYPE_ARP);
    fixed_header(buf, MN_ARP_OP_REPLY);
    copy(buf + OFF_SHA, our_mac, MN_ETH_ADDR_LEN);
    copy(buf + OFF_SPA, our_ip, MN_IPV4_ADDR_LEN);
    copy(buf + OFF_THA, request + OFF_SHA, MN_ETH_ADDR_LEN);
    copy(buf + OFF_TPA, request + OFF_SPA, MN_IPV4_ADDR_LEN);
    return MN_ARP_FRAME_LEN;
}

uint16_t mn_arp_opcode(const uint8_t *frame, uint16_t len)
{
    if (!is_arp(frame, len))
        return 0;
    return mn_get16(frame + OFF_OPER);
}

uint8_t mn_arp_wants_us(const uint8_t *frame, uint16_t len,
                        const uint8_t *our_ip)
{
    if (mn_arp_opcode(frame, len) != MN_ARP_OP_REQUEST)
        return 0;
    return eq(frame + OFF_TPA, our_ip, MN_IPV4_ADDR_LEN);
}

const uint8_t *mn_arp_sender_mac(const uint8_t *frame) { return frame + OFF_SHA; }
const uint8_t *mn_arp_sender_ip(const uint8_t *frame)  { return frame + OFF_SPA; }
const uint8_t *mn_arp_target_ip(const uint8_t *frame)  { return frame + OFF_TPA; }

void mn_arp_cache_init(mn_arp_cache *c)
{
    uint8_t i;
    for (i = 0; i < MN_ARP_CACHE; i++)
        c->e[i].valid = 0;
    c->next = 0;
}

void mn_arp_cache_learn(mn_arp_cache *c, const uint8_t *ip, const uint8_t *mac)
{
    uint8_t i;
    mn_arp_entry *e;

    for (i = 0; i < MN_ARP_CACHE; i++) {          /* refresh if known */
        e = &c->e[i];
        if (e->valid && eq(e->ip, ip, MN_IPV4_ADDR_LEN)) {
            copy(e->mac, mac, MN_ETH_ADDR_LEN);
            return;
        }
    }
    for (i = 0; i < MN_ARP_CACHE; i++) {          /* else a free slot */
        if (!c->e[i].valid)
            break;
    }
    if (i == MN_ARP_CACHE) {                      /* else round-robin */
        i = c->next;
        c->next = (uint8_t)((c->next + 1) % MN_ARP_CACHE);
    }
    e = &c->e[i];
    copy(e->ip, ip, MN_IPV4_ADDR_LEN);
    copy(e->mac, mac, MN_ETH_ADDR_LEN);
    e->valid = 1;
}

const uint8_t *mn_arp_cache_lookup(const mn_arp_cache *c, const uint8_t *ip)
{
    uint8_t i;
    for (i = 0; i < MN_ARP_CACHE; i++)
        if (c->e[i].valid && eq(c->e[i].ip, ip, MN_IPV4_ADDR_LEN))
            return c->e[i].mac;
    return 0;
}
