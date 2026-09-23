#ifndef MN_UDP_H
#define MN_UDP_H

#include <stdint.h>

#define MN_UDP_HDR_LEN 8

/* Writes the header at udp for payload_len bytes following it, with the
 * checksum computed over the pseudo-header for src/dst. Returns the
 * whole datagram length. */
uint16_t mn_udp_build(uint8_t *udp, const uint8_t *src_ip,
                      const uint8_t *dst_ip, uint16_t sport, uint16_t dport,
                      uint16_t payload_len);

/* 1 if the datagram's length field matches udp_len and its checksum
 * verifies (a zero checksum means "none" and is accepted). */
uint8_t mn_udp_valid(const uint8_t *src_ip, const uint8_t *dst_ip,
                     const uint8_t *udp, uint16_t udp_len);

uint16_t mn_udp_sport(const uint8_t *udp);
uint16_t mn_udp_dport(const uint8_t *udp);
uint16_t mn_udp_len(const uint8_t *udp);          /* header + payload */
const uint8_t *mn_udp_payload(const uint8_t *udp);

#endif
