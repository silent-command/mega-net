/* ICMP, as much as answering ping needs: recognise an echo request and
 * build the echo reply frame. */
#ifndef MN_ICMP_H
#define MN_ICMP_H

#include <stdint.h>

#define MN_ICMP_ECHO_REPLY   0
#define MN_ICMP_ECHO_REQUEST 8
#define MN_ICMP_HDR_LEN      8

/* 1 if frame (len bytes, Ethernet header included) is a valid IPv4
 * packet carrying an ICMP echo request for our_ip with a good ICMP
 * checksum. */
uint8_t mn_icmp_is_echo_request(const uint8_t *frame, uint16_t len,
                                const uint8_t *our_ip);

/* Builds the complete reply frame in out from a request already checked
 * with mn_icmp_is_echo_request. Returns the frame length. */
uint16_t mn_icmp_build_echo_reply(uint8_t *out, const uint8_t *request,
                                  uint16_t len, const uint8_t *our_mac,
                                  const uint8_t *our_ip);

#endif
