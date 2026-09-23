#include "mn_icmp.h"
#include "mn_ether.h"
#include "mn_ipv4.h"
#include "mn_byteorder.h"
#include "mn_checksum.h"

#define OFF_TYPE 0
#define OFF_CODE 1
#define OFF_CSUM 2

uint8_t mn_icmp_is_echo_request(const uint8_t *frame, uint16_t len,
                                const uint8_t *our_ip)
{
    const uint8_t *ip, *icmp;
    uint16_t plen;

    if (len < MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + MN_ICMP_HDR_LEN)
        return 0;
    if (mn_ether_type(frame) != MN_ETHERTYPE_IPV4)
        return 0;
    ip = frame + MN_ETH_HDR_LEN;
    if (!mn_ipv4_valid(ip, (uint16_t)(len - MN_ETH_HDR_LEN)))
        return 0;
    if (mn_ipv4_proto(ip) != MN_IPPROTO_ICMP)
        return 0;
    if (!mn_ipv4_addr_eq(mn_ipv4_dst(ip), our_ip))
        return 0;
    icmp = mn_ipv4_payload(ip);
    plen = mn_ipv4_payload_len(ip);
    if (plen < MN_ICMP_HDR_LEN)
        return 0;
    if (icmp[OFF_TYPE] != MN_ICMP_ECHO_REQUEST || icmp[OFF_CODE] != 0)
        return 0;
    return mn_checksum_valid(icmp, plen);
}

uint16_t mn_icmp_build_echo_reply(uint8_t *out, const uint8_t *request,
                                  uint16_t len, const uint8_t *our_mac,
                                  const uint8_t *our_ip)
{
    const uint8_t *rip = request + MN_ETH_HDR_LEN;
    const uint8_t *ricmp = mn_ipv4_payload(rip);
    uint16_t plen = mn_ipv4_payload_len(rip);
    uint8_t *oip = out + MN_ETH_HDR_LEN;
    uint8_t *oicmp = oip + MN_IPV4_HDR_LEN;
    uint16_t i;
    (void)len;

    /* Back to whoever asked. */
    mn_ether_build(out, mn_ether_src(request), our_mac, MN_ETHERTYPE_IPV4);
    mn_ipv4_build(oip, our_ip, mn_ipv4_src(rip), MN_IPPROTO_ICMP, plen,
                  mn_get16(rip + 4));

    /* Same payload, type changed, checksum redone. */
    for (i = 0; i < plen; i++)
        oicmp[i] = ricmp[i];
    oicmp[OFF_TYPE] = MN_ICMP_ECHO_REPLY;
    mn_put16(oicmp + OFF_CSUM, 0);
    mn_put16(oicmp + OFF_CSUM, mn_checksum(oicmp, plen));

    return (uint16_t)(MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + plen);
}
