/* Written before the implementation, in step 1, so that ARP had a
 * target; green since step 3. The expected bytes are what tcpdump shows
 * for a real request. */
#include "mn_test.h"
#include "../src/net/mn_arp.h"
#include "../src/net/mn_ether.h"

static const uint8_t our_mac[6] = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t our_ip[4]  = { 192, 168, 0, 199 };
static const uint8_t gw_ip[4]   = { 192, 168, 0, 1 };

/* A request for 192.168.0.199 from the gateway, as tcpdump would show it. */
static const uint8_t request_for_us[MN_ARP_FRAME_LEN] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc,
    0x08, 0x06,
    0x00, 0x01,                             /* ethernet */
    0x08, 0x00,                             /* IPv4 */
    0x06, 0x04,                             /* address lengths */
    0x00, 0x01,                             /* request */
    0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc,     /* sender mac */
    192, 168, 0, 1,                         /* sender ip */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* target mac, unknown */
    192, 168, 0, 199                        /* target ip -- us */
};

void test_arp(void)
{
    uint8_t frame[MN_MAX_FRAME];
    uint16_t n;

    static const uint8_t want_request[MN_ARP_FRAME_LEN] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x40, 0x3d, 0xec, 0x11, 0x22, 0x33,
        0x08, 0x06,
        0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,
        0x40, 0x3d, 0xec, 0x11, 0x22, 0x33,
        192, 168, 0, 199,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        192, 168, 0, 1
    };

    mn_suite("arp");

    n = mn_arp_build_request(frame, our_mac, our_ip, gw_ip);
    mn_expect_eq_u16(n, MN_ARP_FRAME_LEN, "request is 42 bytes");
    if (n == MN_ARP_FRAME_LEN)
        mn_expect_eq_bytes(frame, want_request, MN_ARP_FRAME_LEN,
                           "request bytes match tcpdump");
    else
        mn_expect(0, "request bytes match tcpdump", "not built");

    mn_expect_eq_u16(mn_arp_opcode(request_for_us, MN_ARP_FRAME_LEN),
                     MN_ARP_OP_REQUEST, "opcode of a received request");

    mn_expect(mn_arp_wants_us(request_for_us, MN_ARP_FRAME_LEN, our_ip),
              "request for our address is recognised", "");

    mn_expect(!mn_arp_wants_us(request_for_us, MN_ARP_FRAME_LEN, gw_ip),
              "request for another address is ignored", "");

    n = mn_arp_build_reply(frame, request_for_us, our_mac, our_ip);
    mn_expect_eq_u16(n, MN_ARP_FRAME_LEN, "reply is 42 bytes");
    if (n == MN_ARP_FRAME_LEN) {
        mn_expect_eq_u16(mn_ether_type(frame), MN_ETHERTYPE_ARP,
                         "reply is an ARP frame");
        mn_expect_eq_u16(mn_arp_opcode(frame, n), MN_ARP_OP_REPLY,
                         "reply carries opcode 2");
    } else {
        mn_expect(0, "reply is an ARP frame", "not built");
        mn_expect(0, "reply carries opcode 2", "not built");
    }
}
