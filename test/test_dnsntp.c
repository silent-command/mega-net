#include "mn_test.h"
#include "../src/net/mn_net.h"
#include "../src/net/mn_dns.h"
#include "../src/net/mn_ntp.h"
#include "../src/net/mn_ether.h"
#include "../src/net/mn_arp.h"
#include "../src/net/mn_ipv4.h"
#include "../src/net/mn_udp.h"
#include "../src/net/mn_byteorder.h"
#include "stub_link.h"

static const uint8_t our_mac[6] = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t srv_mac[6] = { 0x02, 0x00, 0x5e, 0x00, 0x00, 0x01 };
static const uint8_t our_ip[4]  = { 192, 168, 0, 199 };
static const uint8_t mask[4]    = { 255, 255, 255, 0 };
static const uint8_t gw_ip[4]   = { 192, 168, 0, 1 };
static const uint8_t far_dns[4] = { 9, 9, 9, 9 };
static const uint8_t answer[4]  = { 93, 184, 216, 34 };

/* A frame from src_ip:sport to us:dport carrying payload. */
static uint16_t udp_to_us(uint8_t *f, const uint8_t *src_ip, uint16_t sport,
                          uint16_t dport, const uint8_t *pl, uint16_t n)
{
    uint8_t *ip = f + 14, *udp = ip + 20; uint16_t i, ulen;
    for (i = 0; i < n; i++) udp[8 + i] = pl[i];
    ulen = mn_udp_build(udp, src_ip, our_ip, sport, dport, n);
    mn_ipv4_build(ip, src_ip, our_ip, MN_IPPROTO_UDP, ulen, 5);
    mn_ether_build(f, our_mac, srv_mac, MN_ETHERTYPE_IPV4);
    return (uint16_t)(34 + ulen);
}

/* Resolve the gateway for the stub: answer the stack's ARP request. */
static void arp_gateway(stub_link *s, mn_net *net)
{
    uint8_t f[64]; uint16_t len, n;
    const uint8_t *sent = stub_link_sent(s, (uint8_t)(s->tx_count - 1), &len);
    n = mn_arp_build_reply(f, sent, srv_mac, gw_ip);
    stub_link_inject(s, f, n); mn_net_poll(net);
}

/* A DNS response to the query in the last sent frame: a CNAME first
 * (with a compression pointer), then the A record. If rcode, an error. */
static uint16_t dns_response(uint8_t *f, stub_link *s, uint16_t *qport, uint8_t rcode,
                             uint16_t id_bump)
{
    uint16_t len, o, qlen; uint8_t m[200], i;
    const uint8_t *sent = stub_link_sent(s, (uint8_t)(s->tx_count - 1), &len);
    const uint8_t *q = sent + 34 + 8;                 /* the query */
    *qport = mn_udp_sport(sent + 34);
    qlen = (uint16_t)(mn_udp_len(sent + 34) - 8);
    for (i = 0; i < qlen; i++) m[i] = q[i];
    mn_put16(m, (uint16_t)(mn_get16(q) + id_bump));
    mn_put16(m + 2, (uint16_t)(0x8180 | rcode));
    mn_put16(m + 6, rcode ? 0 : 2);
    o = qlen;
    if (!rcode) {
        m[o++] = 0xc0; m[o++] = 12;                  /* name -> question */
        mn_put16(m + o, 5); o += 2;                  /* CNAME */
        mn_put16(m + o, 1); o += 2;
        mn_put32(m + o, 300); o += 4;
        mn_put16(m + o, 6); o += 2;                  /* rdlength */
        m[o++] = 3; m[o++] = 'w'; m[o++] = 'w'; m[o++] = 'w'; m[o++] = 0xc0; m[o++] = 12;
        m[o++] = 0xc0; m[o++] = 12;
        mn_put16(m + o, 1); o += 2;                  /* A */
        mn_put16(m + o, 1); o += 2;
        mn_put32(m + o, 300); o += 4;
        mn_put16(m + o, 4); o += 2;
        for (i = 0; i < 4; i++) m[o++] = answer[i];
    }
    return udp_to_us(f, far_dns, 53, *qport, m, o);
}

void test_dns(void)
{
    stub_link s; mn_netif nif; mn_net net; mn_dns d;
    uint8_t f[300]; uint16_t n, len, qport, now = 500;
    const uint8_t *sent; const uint8_t *q;

    mn_suite("dns");
    stub_link_init(&s, &nif, our_mac); mn_net_init(&net, &nif);
    mn_net_set_ip(&net, our_ip, mask, gw_ip);
    d.sock = 0xff;

    mn_dns_start(&d, &net, "example.com", far_dns, now);
    mn_expect_eq_u16(d.state, MN_DNS_WAITING, "start -> WAITING");
    mn_expect(d.pending, "off-link server, gateway unknown: pending", "");
    sent = stub_link_sent(&s, 0, &len);
    mn_expect(sent && mn_arp_opcode(sent, len) == MN_ARP_OP_REQUEST, "ARP for the gateway first", "");
    arp_gateway(&s, &net);
    mn_dns_poll(&d, &net, now);
    mn_expect(!d.pending, "query sent once the gateway is known", "");
    sent = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
    q = sent + 34 + 8;
    mn_expect_eq_u16(mn_udp_dport(sent + 34), 53, "to port 53");
    mn_expect_eq_u16(mn_get16(q + 2), 0x0100, "RD set, nothing else");
    mn_expect_eq_u16(mn_get16(q + 4), 1, "one question");
    mn_expect_eq_bytes(q + 12, (const uint8_t *)"\x07" "example" "\x03" "com", 12, "labels");
    mn_expect_eq_u16(q[24], 0, "root label");
    mn_expect_eq_u16(mn_get16(q + 25), 1, "type A");
    mn_expect_eq_u16(mn_get16(q + 27), 1, "class IN");

    n = dns_response(f, &s, &qport, 0, 1);
    stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dns_poll(&d, &net, now);
    mn_expect_eq_u16(d.state, MN_DNS_WAITING, "wrong id ignored");
    mn_dns_poll(&d, &net, (uint16_t)(now + 120));
    mn_expect_eq_u16(d.tries, 2, "retransmitted after 2 s");
    n = dns_response(f, &s, &qport, 0, 0);
    stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dns_poll(&d, &net, now);
    mn_expect_eq_u16(d.state, MN_DNS_DONE, "answer -> DONE (past a CNAME)");
    mn_expect_eq_bytes(d.ip, answer, 4, "the address");
    mn_expect(d.sock == 0xff, "socket released", "");

    mn_dns_start(&d, &net, "nx.example", far_dns, now);
    n = dns_response(f, &s, &qport, 3, 0);             /* NXDOMAIN */
    stub_link_inject(&s, f, n); mn_net_poll(&net); mn_dns_poll(&d, &net, now);
    mn_expect_eq_u16(d.state, MN_DNS_FAILED, "NXDOMAIN -> FAILED");

    mn_dns_start(&d, &net, "bad..name", far_dns, now);
    mn_expect_eq_u16(d.state, MN_DNS_FAILED, "malformed name refused");

    mn_dns_start(&d, &net, "silent.example", far_dns, 0);
    for (n = 1; n < 5; n++) mn_dns_poll(&d, &net, (uint16_t)(n * 120));
    mn_expect_eq_u16(d.state, MN_DNS_FAILED, "no answer: FAILED after retries");
}

void test_ntp(void)
{
    stub_link s; mn_netif nif; mn_net net; mn_ntp t; mn_civil c;
    uint8_t f[128], m[48]; uint16_t n, len, i;
    const uint8_t *sent;

    mn_suite("ntp");
    stub_link_init(&s, &nif, our_mac); mn_net_init(&net, &nif);
    mn_net_set_ip(&net, our_ip, mask, gw_ip);
    t.sock = 0xff;

    mn_ntp_start(&t, &net, far_dns, 10);
    arp_gateway(&s, &net); mn_ntp_poll(&t, &net, 10);
    sent = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
    mn_expect_eq_u16(mn_udp_dport(sent + 34), 123, "to port 123");
    mn_expect_eq_u16(mn_udp_len(sent + 34), 56, "48-byte request");
    mn_expect_eq_u16(sent[42], 0x23, "LI 0, version 4, mode 3");

    for (i = 0; i < 48; i++) m[i] = 0;
    m[0] = 0x24;                                        /* v4, mode 4 */
    mn_put32(m + 40, 3997526400UL);
    n = udp_to_us(f, far_dns, 123, mn_udp_sport(sent + 34), m, 48);
    stub_link_inject(&s, f, n); mn_net_poll(&net); mn_ntp_poll(&t, &net, 11);
    mn_expect_eq_u16(t.state, MN_NTP_DONE, "reply -> DONE");
    mn_expect(t.seconds == 3997526400UL, "transmit timestamp taken", "");

    mn_ntp_civil(3997526400UL, 0, &c);
    mn_expect(c.year == 2026 && c.month == 9 && c.day == 4 && c.hour == 16 && c.minute == 0 && c.second == 0,
              "2026-09-04 16:00:00 UTC", "");
    mn_expect_eq_u16(c.weekday, 5, "a Friday");
    mn_ntp_civil(3997526400UL, 600, &c);
    mn_expect(c.day == 5 && c.hour == 2, "+10:00 offset crosses midnight", "");
    mn_ntp_civil(3997526400UL, -420, &c);
    mn_expect(c.day == 4 && c.hour == 9, "-07:00 offset", "");
    mn_ntp_civil(3918198896UL, 0, &c);
    mn_expect(c.year == 2024 && c.month == 2 && c.day == 29 && c.hour == 12 && c.minute == 34 && c.second == 56,
              "2024-02-29 12:34:56 (leap day)", "");
    mn_ntp_civil(3160771200UL, 0, &c);
    mn_expect(c.year == 2000 && c.month == 2 && c.day == 29, "2000-02-29 (400-year rule)", "");
    mn_ntp_civil(3155673599UL, 0, &c);
    mn_expect(c.year == 1999 && c.month == 12 && c.day == 31 && c.hour == 23 && c.minute == 59 && c.second == 59,
              "1999-12-31 23:59:59", "");
    /* NTP era 1 (RFC 4330): a value below the 1968 pivot is 2036 and on.
     * 0x10000000 + 2^32 s after 1900 is 2044-08-10 03:52:32 UTC. */
    mn_ntp_civil(0x10000000UL, 0, &c);
    mn_expect(c.year == 2044 && c.month == 8 && c.day == 10 && c.hour == 3 && c.minute == 52 && c.second == 32,
              "era 1: 2044-08-10 03:52:32", "");
    mn_ntp_civil(0UL, 0, &c);
    mn_expect(c.year == 2036 && c.month == 2 && c.day == 7 && c.hour == 6 && c.minute == 28 && c.second == 16,
              "era 1 starts 2036-02-07 06:28:16", "");
    mn_ntp_civil(3155673599UL + 1, 0, &c);
    mn_expect(c.year == 2000 && c.month == 1 && c.day == 1 && c.hour == 0, "one second later: 2000-01-01", "");

    mn_ntp_start(&t, &net, far_dns, 0);
    for (n = 1; n < 5; n++) mn_ntp_poll(&t, &net, (uint16_t)(n * 160));
    mn_expect_eq_u16(t.state, MN_NTP_FAILED, "no reply: FAILED after retries");
}
