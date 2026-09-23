#include "mn_test.h"
#include "../src/net/mn_net.h"
#include "../src/net/mn_dhcp.h"
#include "../src/net/mn_ether.h"
#include "../src/net/mn_ipv4.h"
#include "../src/net/mn_udp.h"
#include "../src/net/mn_arp.h"
#include "../src/net/mn_byteorder.h"
#include "stub_link.h"

static const uint8_t our_mac[6] = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t srv_mac[6] = { 0x02, 0x00, 0x5e, 0x00, 0x00, 0x01 };
static const uint8_t srv_ip[4]  = { 192, 168, 0, 1 };
static const uint8_t lease_ip[4] = { 192, 168, 0, 150 };
static const uint8_t mask[4]    = { 255, 255, 255, 0 };
static const uint8_t dns[4]     = { 9, 9, 9, 9 };
static const uint8_t bcast[4]   = { 255, 255, 255, 255 };
static const uint8_t zero[4]    = { 0, 0, 0, 0 };

/* Builds a server reply of the given type into a frame, addressed to
 * dst_ip (broadcast or the leased address), echoing xid/chaddr from the
 * client message found in the last sent frame. */
static uint8_t overload_style = 0;   /* 1: type and server id go in `file` */
static uint32_t lease_value = 86400;

static uint16_t reply_frame(uint8_t *f, const uint8_t *client_msg, uint8_t type,
                            const uint8_t *dst_ip, uint16_t xid_bump)
{
    uint8_t *ip = f + 14, *udp = ip + 20, *m = udp + 8;
    uint16_t i, o, ulen;
    for (i = 0; i < 300; i++) m[i] = 0;
    m[0] = 2; m[1] = 1; m[2] = 6;
    for (i = 0; i < 4; i++) m[4 + i] = client_msg[4 + i];
    m[7] = (uint8_t)(m[7] + xid_bump);
    for (i = 0; i < 4; i++) m[16 + i] = lease_ip[i];       /* yiaddr */
    for (i = 0; i < 16; i++) m[28 + i] = client_msg[28 + i];
    m[236] = 0x63; m[237] = 0x82; m[238] = 0x53; m[239] = 0x63;
    o = 240;
    if (overload_style) {
        uint16_t p = 108;                            /* the file field */
        m[p++] = 53; m[p++] = 1; m[p++] = type;
        m[p++] = 54; m[p++] = 4; for (i = 0; i < 4; i++) m[p++] = srv_ip[i];
        m[p++] = 255;
        m[o++] = 52; m[o++] = 1; m[o++] = 1;          /* overload: file */
    } else {
        m[o++] = 53; m[o++] = 1; m[o++] = type;
        m[o++] = 54; m[o++] = 4; for (i = 0; i < 4; i++) m[o++] = srv_ip[i];
    }
    m[o++] = 1;  m[o++] = 4; for (i = 0; i < 4; i++) m[o++] = mask[i];
    m[o++] = 3;  m[o++] = 4; for (i = 0; i < 4; i++) m[o++] = srv_ip[i];
    m[o++] = 6;  m[o++] = 4; for (i = 0; i < 4; i++) m[o++] = dns[i];
    m[o++] = 51; m[o++] = 4; mn_put32(m + o, lease_value); o += 4;
    m[o++] = 255;
    ulen = mn_udp_build(udp, srv_ip, dst_ip, 67, 68, 300);
    mn_ipv4_build(ip, srv_ip, dst_ip, MN_IPPROTO_UDP, ulen, 9);
    mn_ether_build(f, our_mac, srv_mac, MN_ETHERTYPE_IPV4);
    return (uint16_t)(14 + 20 + ulen);
}

static const uint8_t *last_client_msg(stub_link *s, uint16_t *len)
{
    const uint8_t *sent = stub_link_sent(s, (uint8_t)(s->tx_count - 1), len);
    return sent ? sent + 14 + 20 + 8 : 0;
}

void test_dhcp(void)
{
    stub_link s;
    mn_netif nif;
    mn_net net;
    mn_dhcp d;
    uint8_t f[400];
    uint16_t n, len, now = 1000;
    const uint8_t *sent, *m;

    mn_suite("dhcp");
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    d.sock = 0xff;

    mn_dhcp_start(&d, &net, now);
    mn_expect_eq_u16(d.state, MN_DHCP_SELECTING, "start -> SELECTING");
    mn_expect_eq_u16(s.tx_count, 1, "DISCOVER sent at once");
    sent = stub_link_sent(&s, 0, &len);
    mn_expect(sent != NULL, "frame captured", "");
    if (sent) {
        const uint8_t *ip = sent + 14, *udp = ip + 20;
        mn_expect_eq_bytes(mn_ether_dst(sent), mn_eth_broadcast, 6, "to the broadcast MAC");
        mn_expect(mn_ipv4_addr_eq(mn_ipv4_dst(ip), bcast), "to 255.255.255.255", "");
        mn_expect(mn_ipv4_addr_eq(mn_ipv4_src(ip), zero), "from 0.0.0.0", "");
        mn_expect_eq_u16(mn_udp_sport(udp), 68, "from port 68");
        mn_expect_eq_u16(mn_udp_dport(udp), 67, "to port 67");
        mn_expect_eq_u16(mn_udp_len(udp), 308, "300-byte BOOTP minimum");
        m = udp + 8;
        mn_expect_eq_u16(m[0], 1, "op BOOTREQUEST");
        mn_expect_eq_u16(m[10], 0x80, "broadcast flag set");
        mn_expect_eq_bytes(m + 28, our_mac, 6, "chaddr is our MAC");
        mn_expect_eq_u16(mn_get32(m + 236) >> 16, 0x6382, "magic cookie");
        mn_expect_eq_u16(m[242], 1, "option 53 = DISCOVER");
    }

    /* Silence: retransmits after the interval, not before. */
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 100));
    mn_expect_eq_u16(s.tx_count, 1, "no retransmit at 2 s");
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 210));
    mn_expect_eq_u16(s.tx_count, 2, "retransmit after 4 s");

    /* A wrong-xid offer is ignored; the right one moves to REQUESTING. */
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 2, bcast, 1);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 220));
    mn_expect_eq_u16(d.state, MN_DHCP_SELECTING, "offer with wrong xid ignored");
    n = reply_frame(f, m, 2, bcast, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 230));
    mn_expect_eq_u16(d.state, MN_DHCP_REQUESTING, "OFFER -> REQUESTING");
    mn_expect_eq_bytes(d.offered_ip, lease_ip, 4, "offered address kept");
    mn_expect_eq_u16(s.tx_count, 3, "REQUEST sent");
    m = last_client_msg(&s, &len);
    if (m) {
        mn_expect_eq_u16(m[242], 3, "option 53 = REQUEST");
        mn_expect_eq_u16(m[243], 50, "option 50 present");
        mn_expect_eq_bytes(m + 245, lease_ip, 4, "requested IP is the offer");
        mn_expect_eq_u16(m[249], 54, "option 54 present");
        mn_expect_eq_bytes(m + 251, srv_ip, 4, "server id echoed");
    }

    /* NAK -> start over with a new DISCOVER. */
    n = reply_frame(f, m, 6, bcast, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 240));
    mn_expect_eq_u16(d.state, MN_DHCP_SELECTING, "NAK -> SELECTING");
    mn_expect_eq_u16(s.tx_count, 4, "new DISCOVER sent");

    /* Offer again, request, then an ACK unicast to the leased address
     * (the server ignoring our broadcast flag): we are unconfigured, so
     * the IPv4 layer must accept it. */
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 2, bcast, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 250));
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 5, lease_ip, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net);
    mn_dhcp_poll(&d, &net, (uint16_t)(now + 260));
    mn_expect_eq_u16(d.state, MN_DHCP_BOUND, "ACK -> BOUND");
    mn_expect_eq_bytes(net.ip, lease_ip, 4, "address configured");
    mn_expect_eq_bytes(net.mask, mask, 4, "mask configured");
    mn_expect_eq_bytes(net.gw, srv_ip, 4, "gateway configured");
    mn_expect_eq_bytes(d.dns, dns, 4, "DNS recorded");
    mn_expect(d.lease_s == 86400, "lease recorded", "");
    mn_expect(d.sock == 0xff, "socket released", "");
    mn_expect_eq_u16(mn_dhcp_poll(&d, &net, (uint16_t)(now + 9999)), MN_DHCP_BOUND,
                     "bound stays bound");

    /* A FiOS-style server: options continued in the file field. */
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    d.sock = 0xff;
    overload_style = 1;
    mn_dhcp_start(&d, &net, 0);
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 2, bcast, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, 1);
    mn_expect_eq_u16(d.state, MN_DHCP_REQUESTING, "OFFER with type in `file` (overload) accepted");
    mn_expect_eq_bytes(d.server_id, srv_ip, 4, "server id found in `file`");
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 5, bcast, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, 2);
    mn_expect_eq_u16(d.state, MN_DHCP_BOUND, "ACK with overload -> BOUND");
    overload_style = 0;

    /* Give up after MAX_TRIES with no server. */
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    d.sock = 0xff;
    mn_dhcp_start(&d, &net, 0);
    for (n = 1; n < 6; n++)
        mn_dhcp_poll(&d, &net, (uint16_t)(n * 250));
    mn_expect_eq_u16(d.state, MN_DHCP_FAILED, "no server: FAILED after retries");
    mn_expect_eq_u16(s.tx_count, 5, "five DISCOVERs in all");

    /* ===== renewal (5.12) ===== */
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    d.sock = 0xff;
    lease_value = 3600;                       /* T1 at 30 min, T2 at 53 min (60 - 7) */
    now = 100;
    mn_dhcp_start(&d, &net, now);
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 2, bcast, 0); stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, now);
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 5, bcast, 0); stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, now);
    mn_expect_eq_u16(d.state, MN_DHCP_BOUND, "bound with a one-hour lease");
    mn_expect(d.t1_m == 30 && d.t2_m == 53, "T1 and T2 set from the lease", "");
    mn_expect_eq_u16(mn_dhcp_lease_left(&d), 60, "lease left reported, in minutes");
    mn_arp_cache_learn(&net.arp, srv_ip, srv_mac);   /* the server's MAC, for the unicast */
#define PASS_SECONDS(n_s) do { uint16_t k_; for (k_ = 0; k_ < (n_s) / 20; k_++) { now = (uint16_t)(now + 1000); mn_dhcp_poll(&d, &net, now); } } while (0)
    {
        uint16_t tx0 = s.tx_count;
        PASS_SECONDS(1780);                   /* 89 polls of 20 s: 29 min 40 s, through a tick wrap */
        mn_expect_eq_u16(s.tx_count, tx0, "quiet until T1");
        mn_expect_eq_u16(d.phase, MN_DHCP_PHASE_BOUND, "still plain bound");
        mn_expect_eq_u16(d.age_m, 29, "minutes counted across the tick's wrap");
        PASS_SECONDS(40);
        mn_expect_eq_u16(d.phase, MN_DHCP_PHASE_RENEWING, "T1 -> renewing");
        mn_expect_eq_u16(s.tx_count, (uint16_t)(tx0 + 1), "a REQUEST went out");
        sent = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
        if (sent) {
            const uint8_t *ip = sent + 14, *udp = ip + 20; m = udp + 8;
            mn_expect_eq_bytes(mn_ether_dst(sent), srv_mac, 6, "unicast to the server's MAC");
            mn_expect(mn_ipv4_addr_eq(mn_ipv4_dst(ip), srv_ip), "to the server's address", "");
            mn_expect(mn_ipv4_addr_eq(mn_ipv4_src(ip), lease_ip), "from our address", "");
            mn_expect_eq_u16(mn_udp_sport(udp), 68, "from port 68");
            mn_expect_eq_u16(m[10], 0x00, "unicast reply asked for");
            mn_expect_eq_bytes(m + 12, lease_ip, 4, "ciaddr is our address");
            mn_expect_eq_u16(m[242], 3, "it is a REQUEST");
            mn_expect_eq_u16(m[243], 55, "no requested-address or server-id options (next is 55)");
        }
        mn_expect(d.sock != 0xff, "socket reopened for the reply", "");
        m = last_client_msg(&s, &len);
        n = reply_frame(f, m, 5, lease_ip, 0); stub_link_inject(&s, f, n); mn_net_poll(&net);
        now = (uint16_t)(now + 50); mn_dhcp_poll(&d, &net, now);
        mn_expect_eq_u16(d.phase, MN_DHCP_PHASE_BOUND, "ACK -> bound again");
        mn_expect_eq_u16(d.age_m, 0, "lease clock restarted");
        mn_expect(d.sock == 0xff, "socket released again", "");
        mn_expect_eq_bytes(net.ip, lease_ip, 4, "address kept");
        mn_expect_eq_u16(d.state, MN_DHCP_BOUND, "state stayed BOUND throughout");
    }
    /* No answer this time: retries a minute apart, rebinding by broadcast
       at T2, and the address dropped at expiry. */
    s.tx_count = 0;                           /* the stub keeps 32 frames; start clean */
    {
        uint16_t tx0;
        PASS_SECONDS(1820);
        mn_expect_eq_u16(d.phase, MN_DHCP_PHASE_RENEWING, "renewing again at T1");
        tx0 = s.tx_count;
        PASS_SECONDS(120);
        mn_expect_eq_u16(s.tx_count, (uint16_t)(tx0 + 2), "two more REQUESTs in two minutes");
        PASS_SECONDS(1260);                   /* to 53 min */
        mn_expect_eq_u16(d.phase, MN_DHCP_PHASE_REBINDING, "T2 -> rebinding");
        sent = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
        if (sent) {
            const uint8_t *ip = sent + 14;
            mn_expect_eq_bytes(mn_ether_dst(sent), mn_eth_broadcast, 6, "rebinding REQUEST is broadcast");
            mn_expect(mn_ipv4_addr_eq(mn_ipv4_dst(ip), bcast), "to 255.255.255.255", "");
        }
        mn_expect_eq_u16(d.state, MN_DHCP_BOUND, "still bound while rebinding");
        tx0 = s.tx_count;
        now = (uint16_t)(now + 1000); mn_dhcp_poll(&d, &net, now);
        PASS_SECONDS(420);                    /* past 60 min */
        mn_expect_eq_u16(d.state, MN_DHCP_SELECTING, "expired -> SELECTING");
        mn_expect_eq_bytes(net.ip, zero, 4, "address dropped");
        mn_expect_eq_u16(mn_dhcp_lease_left(&d), 0, "no lease left");
        mn_expect(s.tx_count > tx0, "DISCOVERs going out", "");
    }
    /* NAK while renewing: the address is gone at once. */
    s.tx_count = 0;
    {
        mn_dhcp_start(&d, &net, now);         /* the machine gave up above; start again */
        m = last_client_msg(&s, &len);
        n = reply_frame(f, m, 2, bcast, 0); stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, now);
        m = last_client_msg(&s, &len);
        n = reply_frame(f, m, 5, bcast, 0); stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, now);
        mn_expect_eq_u16(d.state, MN_DHCP_BOUND, "bound once more");
        s.tx_count = 0;
        PASS_SECONDS(1820);
        mn_expect_eq_u16(d.phase, MN_DHCP_PHASE_RENEWING, "renewing");
        m = last_client_msg(&s, &len);
        n = reply_frame(f, m, 6, lease_ip, 0); stub_link_inject(&s, f, n); mn_net_poll(&net);
        now = (uint16_t)(now + 50); mn_dhcp_poll(&d, &net, now);
        mn_expect_eq_u16(d.state, MN_DHCP_SELECTING, "NAK while renewing -> SELECTING");
        mn_expect_eq_bytes(net.ip, zero, 4, "address dropped on NAK");
    }
    /* A lease with no expiry never renews. */
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    d.sock = 0xff;
    lease_value = 0xFFFFFFFFUL;
    mn_dhcp_start(&d, &net, 0);
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 2, bcast, 0); stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, 1);
    m = last_client_msg(&s, &len);
    n = reply_frame(f, m, 5, bcast, 0); stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dhcp_poll(&d, &net, 2);
    {
        uint16_t tx0 = s.tx_count;
        PASS_SECONDS(20000);
        mn_expect(d.state == MN_DHCP_BOUND && s.tx_count == tx0, "infinite lease: bound, silent", "");
        mn_expect_eq_u16(mn_dhcp_lease_left(&d), 0xFFFF, "reported as 65535");
    }
    lease_value = 86400;
}
