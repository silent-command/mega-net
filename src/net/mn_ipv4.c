#include "mn_ipv4.h"
#include "mn_byteorder.h"
#include "mn_checksum.h"

#define OFF_VER_IHL 0
#define OFF_TOS     1
#define OFF_TOTLEN  2
#define OFF_ID      4
#define OFF_FLAGS   6
#define OFF_TTL     8
#define OFF_PROTO   9
#define OFF_CSUM    10
#define OFF_SRC     12
#define OFF_DST     16

#define DEFAULT_TTL 64
#define FLAG_DF     0x4000

uint16_t mn_ipv4_build(uint8_t *pkt, const uint8_t *src_ip,
                       const uint8_t *dst_ip, uint8_t proto,
                       uint16_t payload_len, uint16_t id)
{
    uint8_t i;

    pkt[OFF_VER_IHL] = 0x45;                /* v4, 5 words */
    pkt[OFF_TOS] = 0;
    mn_put16(pkt + OFF_TOTLEN, (uint16_t)(MN_IPV4_HDR_LEN + payload_len));
    mn_put16(pkt + OFF_ID, id);
    mn_put16(pkt + OFF_FLAGS, FLAG_DF);     /* no fragmentation, ever */
    pkt[OFF_TTL] = DEFAULT_TTL;
    pkt[OFF_PROTO] = proto;
    mn_put16(pkt + OFF_CSUM, 0);
    for (i = 0; i < MN_IPV4_ADDR_LEN; i++) {
        pkt[OFF_SRC + i] = src_ip[i];
        pkt[OFF_DST + i] = dst_ip[i];
    }
    mn_put16(pkt + OFF_CSUM, mn_checksum(pkt, MN_IPV4_HDR_LEN));
    return MN_IPV4_HDR_LEN;
}

uint8_t mn_ipv4_hdr_len(const uint8_t *pkt)
{
    return (uint8_t)((pkt[OFF_VER_IHL] & 0x0f) * 4);
}

uint16_t mn_ipv4_total_len(const uint8_t *pkt)
{
    return mn_get16(pkt + OFF_TOTLEN);
}

uint8_t mn_ipv4_valid(const uint8_t *pkt, uint16_t len)
{
    uint8_t hl;
    uint16_t tl;

    if (len < MN_IPV4_HDR_LEN)
        return 0;
    if ((pkt[OFF_VER_IHL] >> 4) != 4)
        return 0;
    hl = mn_ipv4_hdr_len(pkt);
    if (hl < MN_IPV4_HDR_LEN || hl > len)
        return 0;
    tl = mn_ipv4_total_len(pkt);
    if (tl < hl || tl > len)
        return 0;
    return mn_checksum_valid(pkt, hl);
}

uint8_t mn_ipv4_proto(const uint8_t *pkt)       { return pkt[OFF_PROTO]; }
const uint8_t *mn_ipv4_src(const uint8_t *pkt)  { return pkt + OFF_SRC; }
const uint8_t *mn_ipv4_dst(const uint8_t *pkt)  { return pkt + OFF_DST; }

const uint8_t *mn_ipv4_payload(const uint8_t *pkt)
{
    return pkt + mn_ipv4_hdr_len(pkt);
}

uint16_t mn_ipv4_payload_len(const uint8_t *pkt)
{
    return (uint16_t)(mn_ipv4_total_len(pkt) - mn_ipv4_hdr_len(pkt));
}

uint8_t mn_ipv4_addr_eq(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;
    for (i = 0; i < MN_IPV4_ADDR_LEN; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
