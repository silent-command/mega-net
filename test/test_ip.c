#include "mn_test.h"
#include "../src/net/mn_ether.h"
#include "../src/net/mn_ipv4.h"
#include "../src/net/mn_icmp.h"
#include "../src/net/mn_byteorder.h"
#include "../src/net/mn_checksum.h"

static const uint8_t our_mac[6]  = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t peer_mac[6] = { 0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc };
static const uint8_t our_ip[4]   = { 192, 168, 0, 199 };
static const uint8_t peer_ip[4]  = { 192, 168, 0, 1 };

/* A 98-byte ping as macOS sends it: 56 bytes of ICMP data. */
static uint16_t make_ping(uint8_t *f, const uint8_t *dst_ip)
{
    uint8_t *ip = f + MN_ETH_HDR_LEN, *icmp = ip + MN_IPV4_HDR_LEN;
    uint16_t i;
    mn_ether_build(f, our_mac, peer_mac, MN_ETHERTYPE_IPV4);
    mn_ipv4_build(ip, peer_ip, dst_ip, MN_IPPROTO_ICMP, 64, 0x1234);
    icmp[0] = MN_ICMP_ECHO_REQUEST; icmp[1] = 0;
    mn_put16(icmp + 2, 0);
    mn_put16(icmp + 4, 0x4242);           /* identifier */
    mn_put16(icmp + 6, 7);                /* sequence */
    for (i = 8; i < 64; i++) icmp[i] = (uint8_t)i;
    mn_put16(icmp + 2, mn_checksum(icmp, 64));
    return 98;
}

void test_ipv4(void)
{
    /* A whole packet: validity includes "the bytes the header claims are
     * actually here", so a bare header with a 100-byte payload length
     * must be rejected -- that is tested below, not accidentally. */
    uint8_t h[MN_IPV4_HDR_LEN + 100];
    mn_suite("ipv4");

    mn_expect_eq_u16(mn_ipv4_build(h, our_ip, peer_ip, MN_IPPROTO_UDP, 100, 1),
                     MN_IPV4_HDR_LEN, "header is 20 bytes");
    mn_expect(mn_ipv4_valid(h, sizeof h), "built header validates", "");
    mn_expect(!mn_ipv4_valid(h, MN_IPV4_HDR_LEN),
              "truncated packet rejected", "");
    mn_expect_eq_u16(mn_ipv4_total_len(h), 120, "total length");
    mn_expect_eq_u16(mn_ipv4_proto(h), MN_IPPROTO_UDP, "protocol");
    mn_expect(mn_ipv4_addr_eq(mn_ipv4_src(h), our_ip), "source address", "");
    mn_expect(mn_ipv4_addr_eq(mn_ipv4_dst(h), peer_ip), "destination", "");
    mn_expect_eq_u16(mn_get16(h + 6) & 0x4000, 0x4000, "DF set");

    h[8] = 1;                                   /* change TTL, stale checksum */
    mn_expect(!mn_ipv4_valid(h, sizeof h), "corrupted header rejected", "");
    mn_ipv4_build(h, our_ip, peer_ip, MN_IPPROTO_UDP, 100, 1);
    h[0] = 0x65;                                /* version 6 */
    mn_expect(!mn_ipv4_valid(h, sizeof h), "wrong version rejected", "");
    mn_expect(!mn_ipv4_valid(h, 10), "short buffer rejected", "");
}

void test_icmp(void)
{
    uint8_t req[128], rep[128];
    uint16_t n, m;
    const uint8_t *rip, *ricmp;

    mn_suite("icmp");

    n = make_ping(req, our_ip);
    mn_expect(mn_icmp_is_echo_request(req, n, our_ip),
              "echo request for us is recognised", "");
    mn_expect(!mn_icmp_is_echo_request(req, n, peer_ip),
              "echo request for another address ignored", "");
    req[MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + 20] ^= 0xff;
    mn_expect(!mn_icmp_is_echo_request(req, n, our_ip),
              "bad ICMP checksum rejected", "");

    n = make_ping(req, our_ip);
    m = mn_icmp_build_echo_reply(rep, req, n, our_mac, our_ip);
    mn_expect_eq_u16(m, 98, "reply is 98 bytes");
    mn_expect_eq_bytes(mn_ether_dst(rep), peer_mac, 6, "reply goes to asker");
    mn_expect_eq_bytes(mn_ether_src(rep), our_mac, 6, "reply from us");
    rip = rep + MN_ETH_HDR_LEN;
    mn_expect(mn_ipv4_valid(rip, (uint16_t)(m - MN_ETH_HDR_LEN)),
              "reply IPv4 header validates", "");
    mn_expect(mn_ipv4_addr_eq(mn_ipv4_src(rip), our_ip) &&
              mn_ipv4_addr_eq(mn_ipv4_dst(rip), peer_ip), "IPs swapped", "");
    ricmp = mn_ipv4_payload(rip);
    mn_expect_eq_u16(ricmp[0], MN_ICMP_ECHO_REPLY, "type is echo reply");
    mn_expect(mn_checksum_valid(ricmp, 64), "reply ICMP checksum", "");
    mn_expect_eq_bytes(ricmp + 4, req + MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + 4,
                       60, "id, seq and data echoed");
}
