#include "mn_test.h"
#include "../src/net/mn_ether.h"
#include "stub_link.h"

static const uint8_t our_mac[6]  = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t peer_mac[6] = { 0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc };

void test_ether(void)
{
    uint8_t frame[MN_MAX_FRAME];
    uint16_t n;

    static const uint8_t want[] = {
        0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc,   /* dst */
        0x40, 0x3d, 0xec, 0x11, 0x22, 0x33,   /* src */
        0x08, 0x00                            /* IPv4 */
    };

    mn_suite("ethernet");

    n = mn_ether_build(frame, peer_mac, our_mac, MN_ETHERTYPE_IPV4);
    mn_expect_eq_u16(n, MN_ETH_HDR_LEN, "header is 14 bytes");
    mn_expect_eq_bytes(frame, want, sizeof want, "header bytes");
    mn_expect_eq_u16(mn_ether_type(frame), MN_ETHERTYPE_IPV4,
                     "ethertype reads back");

    mn_expect(mn_ether_is_for_us(frame, peer_mac), "unicast to us", "");
    mn_expect(!mn_ether_is_for_us(frame, our_mac), "unicast to someone else",
              "");

    mn_ether_build(frame, mn_eth_broadcast, our_mac, MN_ETHERTYPE_ARP);
    mn_expect(mn_ether_is_for_us(frame, peer_mac), "broadcast is for us", "");
    mn_expect_eq_u16(mn_ether_type(frame), MN_ETHERTYPE_ARP,
                     "ARP ethertype");
}

void test_link(void)
{
    stub_link s;
    mn_netif nif;
    uint8_t frame[MN_MAX_FRAME];
    uint8_t got[MN_MAX_FRAME];
    uint16_t len = 0;
    const uint8_t *sent;

    mn_suite("stub link");

    stub_link_init(&s, &nif, our_mac);

    mn_expect(!nif.rx(&nif, got, sizeof got, &len),
              "no frame when queue is empty", "");

    mn_ether_build(frame, our_mac, peer_mac, MN_ETHERTYPE_IPV4);
    stub_link_inject(&s, frame, MN_ETH_HDR_LEN);

    mn_expect(nif.rx(&nif, got, sizeof got, &len) == 1,
              "injected frame is received", "");
    mn_expect_eq_u16(len, MN_ETH_HDR_LEN, "received length");
    mn_expect_eq_bytes(got, frame, MN_ETH_HDR_LEN, "received bytes");

    mn_expect(nif.tx(&nif, frame, MN_ETH_HDR_LEN) == 1, "transmit accepted",
              "");
    sent = stub_link_sent(&s, 0, &len);
    mn_expect(sent != NULL, "transmitted frame is captured", "");
    if (sent)
        mn_expect_eq_bytes(sent, frame, MN_ETH_HDR_LEN, "transmitted bytes");

    /* Hardware queue full: the stack has to cope with tx refusing. */
    s.tx_should_fail = 1;
    mn_expect(nif.tx(&nif, frame, MN_ETH_HDR_LEN) == 0,
              "transmit failure is reported", "");
}
