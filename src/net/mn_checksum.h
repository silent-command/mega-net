#ifndef MN_CHECKSUM_H
#define MN_CHECKSUM_H

#include <stdint.h>

/* RFC 1071 Internet checksum over len bytes. Returns the value to store
 * in the header field (already complemented). */
uint16_t mn_checksum(const uint8_t *data, uint16_t len);

/* Verifies a block whose checksum field is still populated. A correct
 * block sums to zero. Returns 1 if valid. */
uint8_t mn_checksum_valid(const uint8_t *data, uint16_t len);

/* Raw ones-complement sum (not complemented), for callers that combine
 * blocks. */
uint16_t mn_checksum_raw(const uint8_t *data, uint16_t len);

/* Transport checksum over the IPv4 pseudo-header (src, dst, zero,
 * proto, length) followed by data. Returns the value to store; a UDP
 * caller must send 0 as 0xFFFF. */
uint16_t mn_checksum_pseudo(const uint8_t *src_ip, const uint8_t *dst_ip,
                            uint8_t proto, const uint8_t *data,
                            uint16_t len);

#endif
