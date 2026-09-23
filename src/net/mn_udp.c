#include "mn_udp.h"
#include "mn_byteorder.h"
#include "mn_checksum.h"
#include "mn_ipv4.h"

#define OFF_SPORT 0
#define OFF_DPORT 2
#define OFF_LEN   4
#define OFF_CSUM  6

uint16_t mn_udp_build(uint8_t *udp, const uint8_t *src_ip,
                      const uint8_t *dst_ip, uint16_t sport, uint16_t dport,
                      uint16_t payload_len)
{
    uint16_t len = (uint16_t)(MN_UDP_HDR_LEN + payload_len);
    uint16_t c;

    mn_put16(udp + OFF_SPORT, sport);
    mn_put16(udp + OFF_DPORT, dport);
    mn_put16(udp + OFF_LEN, len);
    mn_put16(udp + OFF_CSUM, 0);
    c = mn_checksum_pseudo(src_ip, dst_ip, MN_IPPROTO_UDP, udp, len);
    if (c == 0)
        c = 0xffff;                  /* zero means "no checksum" */
    mn_put16(udp + OFF_CSUM, c);
    return len;
}

uint8_t mn_udp_valid(const uint8_t *src_ip, const uint8_t *dst_ip,
                     const uint8_t *udp, uint16_t udp_len)
{
    if (udp_len < MN_UDP_HDR_LEN)
        return 0;
    if (mn_get16(udp + OFF_LEN) != udp_len)
        return 0;
    if (mn_get16(udp + OFF_CSUM) == 0)
        return 1;
    /* With the field populated, the whole thing sums to zero. */
    return mn_checksum_pseudo(src_ip, dst_ip, MN_IPPROTO_UDP, udp, udp_len) == 0;
}

uint16_t mn_udp_sport(const uint8_t *udp) { return mn_get16(udp + OFF_SPORT); }
uint16_t mn_udp_dport(const uint8_t *udp) { return mn_get16(udp + OFF_DPORT); }
uint16_t mn_udp_len(const uint8_t *udp)   { return mn_get16(udp + OFF_LEN); }
const uint8_t *mn_udp_payload(const uint8_t *udp) { return udp + MN_UDP_HDR_LEN; }
