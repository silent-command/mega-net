#include "mn_test.h"
#include "../src/net/mn_net.h"
#include "../src/net/mn_ether.h"
#include "../src/net/mn_arp.h"
#include "../src/net/mn_ipv4.h"
#include "../src/net/mn_icmp.h"
#include "../src/net/mn_byteorder.h"
#include "../src/net/mn_checksum.h"
#include "stub_link.h"

static const uint8_t our_mac[6]  = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t peer_mac[6] = { 0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc };
static const uint8_t our_ip[4]   = { 192, 168, 0, 199 };
static const uint8_t mask[4]     = { 255, 255, 255, 0 };
static const uint8_t peer_ip[4]  = { 192, 168, 0, 1 };
static const uint8_t other_ip[4] = { 192, 168, 0, 7 };

static uint16_t ping_frame(uint8_t *f, const uint8_t *dst_ip)
{
    uint8_t *ip = f + MN_ETH_HDR_LEN, *icmp = ip + MN_IPV4_HDR_LEN;
    uint16_t i;
    mn_ether_build(f, our_mac, peer_mac, MN_ETHERTYPE_IPV4);
    mn_ipv4_build(ip, peer_ip, dst_ip, MN_IPPROTO_ICMP, 64, 0x0101);
    icmp[0] = MN_ICMP_ECHO_REQUEST; icmp[1] = 0; mn_put16(icmp + 2, 0);
    mn_put16(icmp + 4, 0x1234); mn_put16(icmp + 6, 1);
    for (i = 8; i < 64; i++) icmp[i] = (uint8_t)(i * 3);
    mn_put16(icmp + 2, mn_checksum(icmp, 64));
    return 98;
}

void test_net(void)
{
    stub_link s;
    mn_netif nif;
    mn_net net;
    uint8_t f[128];
    uint16_t n, len;
    const uint8_t *sent;

    mn_suite("net");
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    mn_net_set_ip(&net, our_ip, mask, peer_ip);

    mn_expect(!mn_net_poll(&net), "poll with nothing pending returns 0", "");

    /* ARP request for us -> reply to the asker. */
    n = mn_arp_build_request(f, peer_mac, peer_ip, our_ip);
    stub_link_inject(&s, f, n);
    mn_expect(mn_net_poll(&net), "poll pulls the ARP request", "");
    sent = stub_link_sent(&s, 0, &len);
    mn_expect(sent != NULL, "a reply was sent", "");
    if (sent) {
        mn_expect_eq_u16(mn_arp_opcode(sent, len), MN_ARP_OP_REPLY, "it is an ARP reply");
        mn_expect_eq_bytes(mn_ether_dst(sent), peer_mac, 6, "to the asker");
        mn_expect_eq_bytes(sent + 22, our_mac, 6, "sender hardware address is ours");
        mn_expect_eq_bytes(sent + 28, our_ip, 4, "sender protocol address is ours");
    }
    mn_expect_eq_u16(net.arp_replies, 1, "arp_replies counted");

    /* ARP request for someone else -> nothing. */
    n = mn_arp_build_request(f, peer_mac, peer_ip, other_ip);
    stub_link_inject(&s, f, n);
    mn_net_poll(&net);
    mn_expect_eq_u16(s.tx_count, 1, "no reply for another address");

    /* Ping us -> echo reply. */
    n = ping_frame(f, our_ip);
    stub_link_inject(&s, f, n);
    mn_net_poll(&net);
    sent = stub_link_sent(&s, 1, &len);
    mn_expect(sent != NULL, "echo reply was sent", "");
    if (sent) {
        mn_expect_eq_u16(len, 98, "reply is 98 bytes");
        mn_expect_eq_u16((sent + MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN)[0],
                         MN_ICMP_ECHO_REPLY, "type is echo reply");
        mn_expect(mn_ipv4_addr_eq(mn_ipv4_dst(sent + MN_ETH_HDR_LEN), peer_ip),
                  "reply addressed to the pinger", "");
    }
    mn_expect_eq_u16(net.echo_replies, 1, "echo_replies counted");

    /* Ping someone else, and junk -> nothing, no crash. */
    n = ping_frame(f, other_ip);
    stub_link_inject(&s, f, n);
    mn_net_poll(&net);
    for (n = 0; n < 64; n++) f[n] = (uint8_t)(n ^ 0x5a);
    stub_link_inject(&s, f, 64);
    mn_net_poll(&net);
    stub_link_inject(&s, f, 3);                  /* shorter than a header */
    mn_net_poll(&net);
    mn_expect_eq_u16(s.tx_count, 2, "nothing sent for other/junk/short");
    mn_expect_eq_u16(net.rx_frames, 6, "every frame counted");

    /* Link refuses -> failure counted, no crash. */
    s.tx_should_fail = 1;
    n = ping_frame(f, our_ip);
    stub_link_inject(&s, f, n);
    mn_net_poll(&net);
    mn_expect_eq_u16(net.tx_failures, 1, "tx failure counted");
}
