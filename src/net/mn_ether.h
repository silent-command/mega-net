#ifndef MN_ETHER_H
#define MN_ETHER_H

#include <stdint.h>
#include "mn_netif.h"

#define MN_ETH_HDR_LEN 14

#define MN_ETHERTYPE_IPV4 0x0800u
#define MN_ETHERTYPE_ARP  0x0806u

/* Offsets within the frame. */
#define MN_ETH_OFF_DST  0
#define MN_ETH_OFF_SRC  6
#define MN_ETH_OFF_TYPE 12

extern const uint8_t mn_eth_broadcast[MN_ETH_ADDR_LEN];

/* Writes a 14-byte header at buf. Returns MN_ETH_HDR_LEN. */
uint16_t mn_ether_build(uint8_t *buf, const uint8_t *dst, const uint8_t *src,
                        uint16_t ethertype);

uint16_t mn_ether_type(const uint8_t *frame);
const uint8_t *mn_ether_dst(const uint8_t *frame);
const uint8_t *mn_ether_src(const uint8_t *frame);

/* 1 if the frame is addressed to us or to broadcast. */
uint8_t mn_ether_is_for_us(const uint8_t *frame, const uint8_t *our_mac);

#endif
