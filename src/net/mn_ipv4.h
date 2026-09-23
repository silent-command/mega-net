/* IPv4 headers: build, validate, read. No options support on transmit;
 * received headers with options are honoured through the IHL field. */
#ifndef MN_IPV4_H
#define MN_IPV4_H

#include <stdint.h>

#define MN_IPV4_HDR_LEN 20
#define MN_IPV4_ADDR_LEN 4

#define MN_IPPROTO_ICMP 1
#define MN_IPPROTO_TCP  6
#define MN_IPPROTO_UDP  17

/* Writes a 20-byte header at pkt for payload_len bytes of proto, with
 * the checksum filled in. Returns MN_IPV4_HDR_LEN. */
uint16_t mn_ipv4_build(uint8_t *pkt, const uint8_t *src_ip,
                       const uint8_t *dst_ip, uint8_t proto,
                       uint16_t payload_len, uint16_t id);

/* 1 if pkt (len bytes available) is a version-4 header whose IHL and
 * total length fit and whose checksum verifies. */
uint8_t mn_ipv4_valid(const uint8_t *pkt, uint16_t len);

uint8_t  mn_ipv4_hdr_len(const uint8_t *pkt);      /* IHL * 4 */
uint16_t mn_ipv4_total_len(const uint8_t *pkt);
uint8_t  mn_ipv4_proto(const uint8_t *pkt);
const uint8_t *mn_ipv4_src(const uint8_t *pkt);
const uint8_t *mn_ipv4_dst(const uint8_t *pkt);

/* Payload start and length, from a header already known valid. */
const uint8_t *mn_ipv4_payload(const uint8_t *pkt);
uint16_t mn_ipv4_payload_len(const uint8_t *pkt);

uint8_t mn_ipv4_addr_eq(const uint8_t *a, const uint8_t *b);

#endif
