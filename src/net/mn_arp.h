/* ARP over Ethernet for IPv4 (RFC 826): build requests and replies,
 * recognise requests for our address. Resolution and caching live one
 * layer up, with IPv4, which is what needs them. */
#ifndef MN_ARP_H
#define MN_ARP_H

#include <stdint.h>
#include "mn_netif.h"

#define MN_IPV4_ADDR_LEN 4
#define MN_ARP_PAYLOAD_LEN 28
#define MN_ARP_FRAME_LEN (14 + MN_ARP_PAYLOAD_LEN)

#define MN_ARP_OP_REQUEST 1
#define MN_ARP_OP_REPLY   2

/* Builds a complete Ethernet frame containing an ARP request asking who
 * owns target_ip. Returns the frame length, or 0 on failure. */
uint16_t mn_arp_build_request(uint8_t *buf, const uint8_t *sender_mac,
                              const uint8_t *sender_ip,
                              const uint8_t *target_ip);

/* Builds a reply to a received request. Returns the frame length, or 0. */
uint16_t mn_arp_build_reply(uint8_t *buf, const uint8_t *request,
                            const uint8_t *our_mac, const uint8_t *our_ip);

/* Reads the opcode from an ARP frame. Returns 0 if it is not valid ARP. */
uint16_t mn_arp_opcode(const uint8_t *frame, uint16_t len);

/* 1 if this is a request for our_ip and we should answer it. */
uint8_t mn_arp_wants_us(const uint8_t *frame, uint16_t len,
                        const uint8_t *our_ip);

/* Fields of a received ARP frame, valid once mn_arp_opcode() != 0. */
const uint8_t *mn_arp_sender_mac(const uint8_t *frame);
const uint8_t *mn_arp_sender_ip(const uint8_t *frame);
const uint8_t *mn_arp_target_ip(const uint8_t *frame);

/* A small cache. Four entries is enough for a client that talks to a
 * gateway and a few peers; replacement is round-robin. */
#define MN_ARP_CACHE 4

typedef struct {
    uint8_t ip[MN_IPV4_ADDR_LEN];
    uint8_t mac[MN_ETH_ADDR_LEN];
    uint8_t valid;
} mn_arp_entry;

typedef struct {
    mn_arp_entry e[MN_ARP_CACHE];
    uint8_t next;
} mn_arp_cache;

void mn_arp_cache_init(mn_arp_cache *c);
void mn_arp_cache_learn(mn_arp_cache *c, const uint8_t *ip, const uint8_t *mac);
const uint8_t *mn_arp_cache_lookup(const mn_arp_cache *c, const uint8_t *ip);

#endif
