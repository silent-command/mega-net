#include "mn_test.h"
#include "../src/net/mn_net.h"
#include "../src/net/mn_ether.h"
#include "../src/net/mn_arp.h"
#include "../src/net/mn_ipv4.h"
#include "../src/net/mn_udp.h"
#include "../src/net/mn_byteorder.h"
#include "../src/net/mn_checksum.h"
#include "stub_link.h"

static const uint8_t our_mac[6]  = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t peer_mac[6] = { 0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc };
static const uint8_t gw_mac[6]   = { 0x02, 0x00, 0x5e, 0x00, 0x00, 0x01 };
static const uint8_t our_ip[4]   = { 192, 168, 0, 199 };
static const uint8_t mask[4]     = { 255, 255, 255, 0 };
static const uint8_t gw_ip[4]    = { 192, 168, 0, 1 };
static const uint8_t peer_ip[4]  = { 192, 168, 0, 7 };
static const uint8_t far_ip[4]   = { 8, 8, 8, 8 };
static const uint8_t bcast[4]    = { 255, 255, 255, 255 };

/* A UDP frame from peer to us:port with the given payload. */
static uint16_t udp_frame(uint8_t *f, const uint8_t *src_ip, uint16_t sport,
                          uint16_t dport, const uint8_t *pl, uint16_t n)
{
    uint8_t *ip = f + MN_ETH_HDR_LEN, *udp = ip + MN_IPV4_HDR_LEN;
    uint16_t i, ulen;
    for (i = 0; i < n; i++) udp[MN_UDP_HDR_LEN + i] = pl[i];
    ulen = mn_udp_build(udp, src_ip, our_ip, sport, dport, n);
    mn_ipv4_build(ip, src_ip, our_ip, MN_IPPROTO_UDP, ulen, 77);
    mn_ether_build(f, our_mac, peer_mac, MN_ETHERTYPE_IPV4);
    return (uint16_t)(MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + ulen);
}

void test_udp(void)
{
    uint8_t udp[64];
    mn_suite("udp");

    mn_expect_eq_u16(mn_udp_build(udp, our_ip, peer_ip, 1234, 53, 10),
                     18, "datagram length is header + payload");
    mn_expect_eq_u16(mn_udp_sport(udp), 1234, "source port");
    mn_expect_eq_u16(mn_udp_dport(udp), 53, "destination port");
    mn_expect(mn_udp_valid(our_ip, peer_ip, udp, 18), "built datagram validates", "");
    /* A checksum cannot tell src and dst apart (both are summed), but
     * a different address changes the sum. */
    mn_expect(!mn_udp_valid(our_ip, gw_ip, udp, 18),
              "wrong pseudo-header address rejected", "");
    udp[12] ^= 1;
    mn_expect(!mn_udp_valid(our_ip, peer_ip, udp, 18), "corrupted payload rejected", "");
    mn_expect(!mn_udp_valid(our_ip, peer_ip, udp, 17), "length mismatch rejected", "");
    mn_put16(udp + 6, 0);
    mn_expect(mn_udp_valid(our_ip, peer_ip, udp, 18), "zero checksum means none, accepted", "");
}

void test_net_udp(void)
{
    stub_link s;
    mn_netif nif;
    mn_net net;
    uint8_t f[256], buf[64], src[4];
    uint16_t n, len, sport;
    uint8_t sk, sk2, r;
    const uint8_t *sent;
    static const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };

    mn_suite("net: udp, sockets, arp resolution");
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    mn_net_set_ip(&net, our_ip, mask, gw_ip);

    mn_expect(mn_net_next_hop(&net, peer_ip) == peer_ip, "on-link: hop is the peer", "");
    mn_expect(mn_net_next_hop(&net, far_ip) == net.gw, "off-link: hop is the gateway", "");
    mn_expect(mn_net_next_hop(&net, bcast) == bcast, "broadcast: hop is itself", "");

    /* Sockets. */
    sk = mn_net_udp_open(&net, 5000);
    mn_expect(sk != 0xff, "socket opened", "");
    mn_expect(mn_net_udp_open(&net, 5000) == 0xff, "same port twice refused", "");
    mn_expect(!mn_net_udp_recv(&net, sk, buf, sizeof buf, &len, src, &sport),
              "nothing to receive yet", "");

    n = udp_frame(f, peer_ip, 4321, 5000, hello, 5);
    stub_link_inject(&s, f, n);
    mn_net_poll(&net);
    r = mn_net_udp_recv(&net, sk, buf, sizeof buf, &len, src, &sport);
    mn_expect(r, "datagram delivered to the socket", "");
    mn_expect_eq_u16(len, 5, "payload length");
    mn_expect_eq_bytes(buf, hello, 5, "payload bytes");
    mn_expect_eq_bytes(src, peer_ip, 4, "source address");
    mn_expect_eq_u16(sport, 4321, "source port");
    mn_expect(!mn_net_udp_recv(&net, sk, buf, sizeof buf, &len, src, &sport),
              "mailbox freed after receive", "");

    n = udp_frame(f, peer_ip, 1, 5000, hello, 5);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_expect_eq_u16(net.udp_dropped, 1, "second datagram while full is dropped");
    mn_net_udp_recv(&net, sk, buf, sizeof buf, &len, src, &sport);

    n = udp_frame(f, peer_ip, 1, 5001, hello, 5);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_expect_eq_u16(net.udp_dropped, 2, "no socket for the port: dropped");

    f[MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + 9] ^= 0xff;   /* bad UDP checksum */
    n = udp_frame(f, peer_ip, 1, 5000, hello, 5);
    f[MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + 10] ^= 0xff;
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_expect(!mn_net_udp_recv(&net, sk, buf, sizeof buf, &len, src, &sport),
              "corrupted datagram not delivered", "");

    /* Sending: unknown peer -> ARP goes out, nothing else. */
    s.tx_count = 0;
    r = mn_net_send_udp(&net, peer_ip, 5000, 7, hello, 5);
    mn_expect_eq_u16(r, MN_SEND_PENDING, "unknown peer: pending");
    sent = stub_link_sent(&s, 0, &len);
    mn_expect(sent && mn_arp_opcode(sent, len) == MN_ARP_OP_REQUEST,
              "an ARP request was sent", "");
    if (sent)
        mn_expect_eq_bytes(mn_arp_target_ip(sent), peer_ip, 4, "asking for the peer");
    mn_expect_eq_u16(s.tx_count, 1, "and nothing else");

    /* The reply arrives; now it sends. */
    n = mn_arp_build_reply(f, sent, peer_mac, peer_ip);   /* peer answers our request */
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_expect_eq_u16(net.arp_learned, 1, "reply learned");
    s.tx_count = 0;
    r = mn_net_send_udp(&net, peer_ip, 5000, 7, hello, 5);
    mn_expect_eq_u16(r, MN_SEND_OK, "known peer: sent");
    sent = stub_link_sent(&s, 0, &len);
    mn_expect(sent != NULL, "frame captured", "");
    if (sent) {
        const uint8_t *ip = sent + MN_ETH_HDR_LEN, *udp = ip + MN_IPV4_HDR_LEN;
        mn_expect_eq_bytes(mn_ether_dst(sent), peer_mac, 6, "to the peer's MAC");
        mn_expect_eq_u16(len, 14 + 20 + 8 + 5, "frame length");
        mn_expect(mn_ipv4_valid(ip, (uint16_t)(len - 14)), "IPv4 header valid", "");
        mn_expect_eq_u16(mn_ipv4_proto(ip), MN_IPPROTO_UDP, "protocol UDP");
        mn_expect(mn_udp_valid(our_ip, peer_ip, udp, 13), "UDP checksum valid", "");
        mn_expect_eq_u16(mn_udp_sport(udp), 5000, "source port");
        mn_expect_eq_u16(mn_udp_dport(udp), 7, "destination port");
        mn_expect_eq_bytes(mn_udp_payload(udp), hello, 5, "payload");
    }

    /* Off-link: resolve the gateway, then send with the gateway's MAC. */
    s.tx_count = 0;
    r = mn_net_send_udp(&net, far_ip, 5000, 53, hello, 5);
    mn_expect_eq_u16(r, MN_SEND_PENDING, "off-link, gateway unknown: pending");
    sent = stub_link_sent(&s, 0, &len);
    if (sent) mn_expect_eq_bytes(mn_arp_target_ip(sent), gw_ip, 4, "ARP asks for the gateway");
    n = mn_arp_build_reply(f, sent, gw_mac, gw_ip);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    s.tx_count = 0;
    r = mn_net_send_udp(&net, far_ip, 5000, 53, hello, 5);
    mn_expect_eq_u16(r, MN_SEND_OK, "gateway known: sent");
    sent = stub_link_sent(&s, 0, &len);
    if (sent) {
        mn_expect_eq_bytes(mn_ether_dst(sent), gw_mac, 6, "to the gateway's MAC");
        mn_expect(mn_ipv4_addr_eq(mn_ipv4_dst(sent + 14), far_ip), "IP destination is the far host", "");
    }

    /* Broadcast needs no ARP. */
    s.tx_count = 0;
    r = mn_net_send_udp(&net, bcast, 68, 67, hello, 5);
    mn_expect_eq_u16(r, MN_SEND_OK, "broadcast: sent at once");
    sent = stub_link_sent(&s, 0, &len);
    if (sent) mn_expect_eq_bytes(mn_ether_dst(sent), mn_eth_broadcast, 6, "to ff:ff:ff:ff:ff:ff");

    sk2 = mn_net_udp_open(&net, 6000);
    mn_net_udp_close(&net, sk);
    mn_expect(mn_net_udp_open(&net, 5000) != 0xff, "port reusable after close", "");
    (void)sk2;

    mn_expect(mn_net_send_udp(&net, peer_ip, 1, 2, f, MN_UDP_MAX_PAYLOAD + 1) == MN_SEND_FAILED,
              "oversize payload refused", "");
}
