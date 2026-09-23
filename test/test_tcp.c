#include "mn_test.h"
#include <string.h>
#include "../src/net/mn_net.h"
#include "../src/net/mn_tcp.h"
#include "../src/net/mn_xmem.h"
#include "../src/net/mn_ether.h"
#include "../src/net/mn_arp.h"
#include "../src/net/mn_ipv4.h"
#include "../src/net/mn_byteorder.h"
#include "../src/net/mn_checksum.h"
#include "stub_link.h"

static const uint8_t our_mac[6]  = { 0x40, 0x3d, 0xec, 0x11, 0x22, 0x33 };
static const uint8_t peer_mac[6] = { 0x02, 0x00, 0x5e, 0xaa, 0xbb, 0xcc };
static const uint8_t our_ip[4]   = { 192, 168, 0, 199 };
static const uint8_t mask[4]     = { 255, 255, 255, 0 };
static const uint8_t gw_ip[4]    = { 192, 168, 0, 1 };
static const uint8_t peer_ip[4]  = { 192, 168, 0, 70 };

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

/* --- a synthetic peer --- */
static stub_link s;
static mn_netif nif;
static mn_net net;
static mn_tcp socks[4];
static mn_tcp_set set;
#define t (socks[0])
static uint16_t now;
static uint32_t peer_seq;              /* the peer's next sequence number */
static uint16_t peer_port = 70, our_port;

typedef struct { uint8_t flags; uint32_t seq, ack; uint16_t win, dlen; const uint8_t *data; } seg_t;

/* Parses the n-th most recent frame the stack sent (0 = latest). */
static uint8_t sent_seg(uint8_t back, seg_t *o)
{
    uint16_t len, hlen;
    const uint8_t *f, *ip, *tcp;
    if (s.tx_count <= back) return 0;
    f = stub_link_sent(&s, (uint8_t)(s.tx_count - 1 - back), &len);
    if (!f || mn_ether_type(f) != MN_ETHERTYPE_IPV4) return 0;
    ip = f + 14;
    if (mn_ipv4_proto(ip) != MN_IPPROTO_TCP) return 0;
    tcp = mn_ipv4_payload(ip);
    hlen = (uint16_t)((tcp[12] >> 4) * 4);
    o->flags = tcp[13]; o->seq = mn_get32(tcp + 4); o->ack = mn_get32(tcp + 8);
    o->win = mn_get16(tcp + 14); o->dlen = (uint16_t)(mn_ipv4_payload_len(ip) - hlen);
    o->data = tcp + hlen;
    our_port = mn_get16(tcp);
    return 1;
}

/* Sends a segment from the peer and lets the stack process it. */
static void peer_send(uint8_t flags, uint32_t seq, uint32_t ack, uint16_t win,
                      const uint8_t *data, uint16_t dlen)
{
    static uint8_t f[MN_MAX_FRAME];
    uint8_t *ip = f + 14, *tcp = ip + 20;
    uint16_t i, hlen = 20;
    mn_put16(tcp, peer_port); mn_put16(tcp + 2, our_port);
    mn_put32(tcp + 4, seq); mn_put32(tcp + 8, ack);
    if (flags & SYN) { hlen = 24; tcp[20] = 2; tcp[21] = 4; mn_put16(tcp + 22, 1400); }
    tcp[12] = (uint8_t)((hlen / 4) << 4); tcp[13] = flags;
    mn_put16(tcp + 14, win); mn_put16(tcp + 16, 0); mn_put16(tcp + 18, 0);
    for (i = 0; i < dlen; i++) tcp[hlen + i] = data[i];
    mn_put16(tcp + 16, mn_checksum_pseudo(peer_ip, our_ip, MN_IPPROTO_TCP, tcp, (uint16_t)(hlen + dlen)));
    mn_ipv4_build(ip, peer_ip, our_ip, MN_IPPROTO_TCP, (uint16_t)(hlen + dlen), 1);
    mn_ether_build(f, our_mac, peer_mac, MN_ETHERTYPE_IPV4);
    stub_link_inject(&s, f, (uint16_t)(34 + hlen + dlen));
    mn_net_poll(&net);
}

static void tick(uint16_t n) { now = (uint16_t)(now + n); mn_tcp_poll(&set, &net, now); }

static void fresh(void)
{
    stub_link_init(&s, &nif, our_mac);
    mn_net_init(&net, &nif);
    mn_net_set_ip(&net, our_ip, mask, gw_ip);
    mn_arp_cache_learn(&net.arp, peer_ip, peer_mac);
    mn_tcp_init(&set, socks, 4, &net, MN_XMEM_BASE);
    now = 1000;
    peer_seq = 0x10000000UL;
}

/* connect and complete the handshake; returns 1 on ESTABLISHED */
static uint8_t handshake(void)
{
    seg_t g;
    mn_tcp_connect(&t, &net, peer_ip, peer_port, now);
    if (!sent_seg(0, &g) || g.flags != SYN) return 0;
    peer_send(SYN | ACK, peer_seq, g.seq + 1, 8192, 0, 0);
    peer_seq++;
    tick(1);
    return t.state == MN_TCP_ESTABLISHED;
}

void test_tcp(void)
{
    seg_t g;
    uint8_t buf[4200], big[3000];
    uint16_t i, n, before;
    static const uint8_t req[] = "/\r\n";

    mn_suite("tcp");
    fresh();

    /* --- handshake --- */
    mn_expect(mn_tcp_connect(&t, &net, peer_ip, peer_port, now), "connect starts", "");
    mn_expect_eq_u16(t.state, MN_TCP_SYN_SENT, "SYN_SENT");
    mn_expect(sent_seg(0, &g), "a segment was sent", "");
    mn_expect_eq_u16(g.flags, SYN, "it is a SYN");
    mn_expect_eq_u16(g.dlen, 0, "no data on the SYN");
    {
        uint16_t len; const uint8_t *f = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
        mn_expect_eq_u16(mn_get16(f + 34 + 22), MN_TCP_MSS, "MSS option offered");
    }
    peer_send(SYN | ACK, peer_seq, g.seq + 1, 8192, 0, 0); peer_seq++;
    mn_expect_eq_u16(t.state, MN_TCP_ESTABLISHED, "SYN-ACK -> ESTABLISHED");
    mn_expect_eq_u16(t.peer_mss, 1400, "peer MSS taken");
    tick(1);
    mn_expect(sent_seg(0, &g) && g.flags == ACK && g.ack == peer_seq, "handshake ACK sent", "");
    mn_expect_eq_u16(g.win, MN_TCP_WINDOW_CAP, "window advertised at the cap");

    /* --- send a request as one segment --- */
    mn_expect_eq_u16(mn_tcp_send(&t, req, 3), 3, "request queued");
    tick(1);
    mn_expect(sent_seg(0, &g) && (g.flags & PSH) && g.dlen == 3, "one PSH segment with the request", "");
    mn_expect_eq_bytes(g.data, req, 3, "request bytes");
    mn_expect_eq_u16(t.inflight, 3, "3 bytes in flight");
    mn_expect_eq_u16(mn_tcp_send(&t, req, 3), 3, "more queued while in flight");
    tick(1);
    mn_expect_eq_u16(t.inflight, 6, "the second segment follows at once: two in flight (5.17)");
    mn_expect(sent_seg(0, &g) && g.dlen == 3 && g.seq == t.snd_una + 3, "with the next sequence number", "");
    peer_send(ACK, peer_seq, t.snd_una + 3, 8192, 0, 0);
    mn_expect_eq_u16(t.inflight, 3, "ACK of the first retires the first");
    peer_send(ACK, peer_seq, t.snd_una + 3, 8192, 0, 0);
    mn_expect_eq_u16(t.inflight, 0, "ACK of the second retires the second");

    /* --- receive data --- */
    for (i = 0; i < 100; i++) big[i] = (uint8_t)i;
    before = s.tx_count;
    peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, big, 100);
    mn_expect_eq_u16(mn_tcp_available(&t), 100, "100 bytes available");
    tick(1);
    mn_expect(s.tx_count > before && sent_seg(0, &g) && g.ack == peer_seq + 100, "ACKed at the new rcv_nxt", "");
    n = mn_tcp_recv(&t, buf, 60);
    mn_expect_eq_u16(n, 60, "recv takes what was asked");
    mn_expect_eq_bytes(buf, big, 60, "first 60 bytes");
    n = mn_tcp_recv(&t, buf, 100);
    mn_expect_eq_u16(n, 40, "then the rest");
    mn_expect_eq_bytes(buf, big + 60, 40, "last 40 bytes");
    peer_seq += 100;

    /* --- out of order: dropped, and answered with what we expect --- */
    before = s.tx_count;
    peer_send(ACK | PSH, peer_seq + 500, t.snd_nxt, 8192, big, 10);
    mn_expect_eq_u16(mn_tcp_available(&t), 0, "out-of-order data not taken");
    tick(1);
    mn_expect(s.tx_count > before && sent_seg(0, &g) && g.ack == peer_seq, "duplicate ACK for the gap", "");
    mn_expect_eq_u16(t.stat_ooo, 1, "counted");

    /* --- window: more than the ring takes is left for the peer to resend --- */
    for (i = 0; i < 3000; i++) big[i] = (uint8_t)(i * 7);
    peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, big, 1400);
    peer_send(ACK | PSH, peer_seq + 1400, t.snd_nxt, 8192, big + 1400, 1400);
    peer_send(ACK | PSH, peer_seq + 2800, t.snd_nxt, 8192, big + 2800, 200);
    tick(1);
    mn_expect_eq_u16(mn_tcp_available(&t), 3000, "3000 bytes taken in order");
    peer_seq += 3000;
    /* fill the rest of the ring: 4096 - 3000 = 1096 free; offer 1400 */
    peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, big, 1400);
    mn_expect_eq_u16(mn_tcp_available(&t), MN_TCP_RCVBUF, "ring full: only what fits was taken");
    tick(1);
    sent_seg(0, &g);
    mn_expect_eq_u16(g.win, 0, "zero window advertised");
    mn_expect(g.ack == peer_seq + 1096, "ACK covers exactly what was taken", "");
    peer_seq += 1096;
    /* the application drains: a window update goes out, by the right edge */
    before = s.tx_count;
    n = mn_tcp_recv(&t, buf, 4096);
    mn_expect_eq_u16(n, 4096, "drained");
    tick(1);
    mn_expect(s.tx_count > before && sent_seg(0, &g) && g.win == MN_TCP_WINDOW_CAP && g.ack == peer_seq,
              "window update sent when the edge moves", "");

    /* --- peer closes: EOF, data still readable, then we close --- */
    peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, req, 3);
    peer_seq += 3;
    peer_send(ACK | FIN, peer_seq, t.snd_nxt, 8192, 0, 0);
    peer_seq += 1;
    mn_expect_eq_u16(t.state, MN_TCP_CLOSE_WAIT, "peer FIN -> CLOSE_WAIT");
    mn_expect(t.flags & MN_TCP_F_EOF, "EOF flagged", "");
    mn_expect_eq_u16(mn_tcp_available(&t), 3, "data before the FIN is still there");
    n = mn_tcp_recv(&t, buf, 10);
    mn_expect_eq_u16(n, 3, "and readable after the close");
    tick(1);
    mn_expect(sent_seg(0, &g) && g.ack == peer_seq, "FIN acknowledged", "");
    mn_tcp_close(&t);
    tick(1);
    mn_expect_eq_u16(t.state, MN_TCP_LAST_ACK, "our FIN sent -> LAST_ACK");
    mn_expect(sent_seg(0, &g) && (g.flags & FIN), "it is a FIN", "");
    peer_send(ACK, peer_seq, g.seq + 1, 8192, 0, 0);
    mn_expect_eq_u16(t.state, MN_TCP_CLOSED, "ACK of our FIN -> CLOSED");

    /* --- active close --- */
    fresh();
    mn_expect(handshake(), "handshake (active close test)", "");
    mn_tcp_close(&t); tick(1);
    mn_expect_eq_u16(t.state, MN_TCP_FIN_WAIT_1, "FIN_WAIT_1");
    sent_seg(0, &g);
    peer_send(ACK, peer_seq, g.seq + 1, 8192, 0, 0);
    mn_expect_eq_u16(t.state, MN_TCP_FIN_WAIT_2, "FIN_WAIT_2");
    peer_send(ACK | FIN, peer_seq, t.snd_nxt, 8192, 0, 0);
    mn_expect_eq_u16(t.state, MN_TCP_TIME_WAIT, "TIME_WAIT");
    tick(1);
    mn_expect(sent_seg(0, &g) && g.ack == peer_seq + 1, "peer's FIN acknowledged", "");
    tick(150);
    mn_expect_eq_u16(t.state, MN_TCP_CLOSED, "TIME_WAIT expires -> CLOSED");

    /* --- RST from the peer --- */
    fresh();
    handshake();
    peer_send(RST | ACK, peer_seq, t.snd_nxt, 0, 0, 0);
    mn_expect_eq_u16(t.state, MN_TCP_CLOSED, "RST -> CLOSED");
    mn_expect(t.flags & MN_TCP_F_RESET, "reset flagged", "");

    /* --- refused: RST to our SYN --- */
    fresh();
    mn_tcp_connect(&t, &net, peer_ip, peer_port, now);
    sent_seg(0, &g);
    peer_send(RST | ACK, 0, g.seq + 1, 0, 0, 0);
    mn_expect_eq_u16(t.state, MN_TCP_CLOSED, "refused -> CLOSED");
    mn_expect(t.flags & MN_TCP_F_REFUSED, "refused flagged", "");

    /* --- SYN retransmission and timeout --- */
    fresh();
    mn_tcp_connect(&t, &net, peer_ip, peer_port, now);
    before = s.tx_count;
    tick(30);
    mn_expect_eq_u16(s.tx_count, before, "no SYN resend before the RTO");
    tick(30);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 1), "SYN resent after 1 s");
    for (i = 0; i < 40; i++) tick(100);
    mn_expect_eq_u16(t.state, MN_TCP_CLOSED, "gives up");
    mn_expect(t.flags & MN_TCP_F_TIMEOUT, "timeout flagged", "");

    /* --- data retransmission --- */
    fresh();
    handshake();
    mn_tcp_send(&t, req, 3); tick(1);
    sent_seg(0, &g);
    before = s.tx_count;
    tick(60);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 1), "data resent after the RTO");
    sent_seg(0, &g);
    mn_expect(g.dlen == 3 && g.seq == t.snd_una, "same bytes, same sequence", "");
    mn_expect_eq_u16(t.stat_retrans, 1, "counted");
    peer_send(ACK, peer_seq, g.seq + 3, 8192, 0, 0);
    mn_expect_eq_u16(t.inflight, 0, "late ACK still retires it");

    /* --- abort --- */
    mn_tcp_abort(&t, &net);
    mn_expect(sent_seg(0, &g) && (g.flags & RST), "abort sends RST", "");
    mn_expect_eq_u16(t.state, MN_TCP_CLOSED, "and closes");

    /* --- connect with the peer's MAC unknown: ARP first, SYN after --- */
    fresh();
    mn_arp_cache_init(&net.arp);
    mn_tcp_connect(&t, &net, peer_ip, peer_port, now);
    {
        uint16_t len; const uint8_t *f = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
        uint8_t reply[64]; uint16_t rn;
        mn_expect(mn_arp_opcode(f, len) == MN_ARP_OP_REQUEST, "ARP request goes first", "");
        mn_expect(t.syn_pending, "SYN pending", "");
        rn = mn_arp_build_reply(reply, f, peer_mac, peer_ip);
        stub_link_inject(&s, reply, rn); mn_net_poll(&net);
        tick(1);
        mn_expect(sent_seg(0, &g) && g.flags == SYN, "SYN follows the ARP reply", "");
        mn_expect(!t.syn_pending, "no longer pending", "");
    }

    /* ===== step 6: socket sets (5.11) ===== */

    /* --- send ring wraps: 700 + 700 bytes through a 1 KB ring --- */
    fresh();
    handshake();
    for (i = 0; i < 1400; i++) big[i] = (uint8_t)(i * 13 + 5);
    mn_expect_eq_u16(mn_tcp_send(&t, big, 700), 700, "700 queued");
    tick(1);
    sent_seg(0, &g);
    mn_expect(g.dlen == 700 && !memcmp(g.data, big, 700), "first 700 on the wire, intact", "");
    peer_send(ACK, peer_seq, g.seq + 700, 8192, 0, 0);
    mn_expect_eq_u16(mn_tcp_send(&t, big + 700, 700), 700, "700 more queued (wraps the ring)");
    tick(1);
    sent_seg(0, &g);
    mn_expect(g.dlen == 700 && !memcmp(g.data, big + 700, 700), "second 700 intact across the wrap", "");
    peer_send(ACK, peer_seq, g.seq + 700, 8192, 0, 0);
    mn_expect_eq_u16(t.snd_len, 0, "all retired");

    /* --- the DMA ring paths, as the ABI's TCP_RECV and TCP_SEND use them
     * (gemini 5.13 traced a fault to them and found them without a host
     * case): recv_x across the receive ring's end, send_x from 28-bit
     * memory across the send ring's end. The host's copy_out and copy_in
     * are memmoves over the pool, so what is checked is the wrap
     * arithmetic, byte for byte. The set is four sockets, 28 KB from the
     * pool's base; the "client's memory" is a spare 4 KB at its top. --- */
#define XBUF (MN_XMEM_BASE + 0xF000UL)
#define XSEG ((uint16_t)(XBUF >> 16))
#define XOFF ((uint16_t)XBUF)
    fresh();
    handshake();
    for (i = 0; i < 3000; i++) big[i] = (uint8_t)(i * 11 + 3);
    peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, big, 1400);
    peer_send(ACK | PSH, peer_seq + 1400, t.snd_nxt, 8192, big + 1400, 1400);
    peer_send(ACK | PSH, peer_seq + 2800, t.snd_nxt, 8192, big + 2800, 200);
    peer_seq += 3000;
    mn_expect_eq_u16(mn_tcp_recv_x(&t, XBUF, 3000), 3000, "recv_x: 3000 bytes to 28-bit memory");
    mn_xmem_read(buf, XSEG, XOFF, 3000);
    mn_expect_eq_bytes(buf, big, 3000, "recv_x: intact");
    mn_expect_eq_u16(t.rcv_head, 3000, "the head at 3000 of 4096");
    tick(1);
    /* 2000 more: 1096 to the ring's end, 904 from its start */
    peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, big, 1400);
    peer_send(ACK | PSH, peer_seq + 1400, t.snd_nxt, 8192, big + 1400, 600);
    peer_seq += 2000;
    mn_expect_eq_u16(mn_tcp_available(&t), 2000, "2000 bytes lie across the ring's end");
    mn_expect_eq_u16(mn_tcp_recv_x(&t, XBUF, 2000), 2000, "recv_x: read across the wrap");
    mn_xmem_read(buf, XSEG, XOFF, 2000);
    mn_expect_eq_bytes(buf, big, 2000, "recv_x: intact across the wrap");
    mn_expect_eq_u16(t.rcv_head, 904, "the head wrapped to 904");
    tick(1);
    /* send_x: a plain send moves the ring's tail to 2800 of 3072, then 700 from 28-bit memory cross its end */
    mn_tcp_send(&t, big, 2800); tick(1);
    peer_send(ACK, peer_seq, t.snd_una + 2800, 8192, 0, 0);
    mn_expect(t.inflight == 0 && t.snd_len == 0 && t.snd_head == 2800, "2800 sent and retired: the tail at 2800 of 3072", "");
    mn_xmem_write(XSEG, XOFF, big + 1400, 700);
    mn_expect_eq_u16(mn_tcp_send_x(&t, XBUF, 700), 700, "send_x: 700 queued from 28-bit memory");
    tick(1);
    sent_seg(0, &g);
    mn_expect(g.dlen == 700 && !memcmp(g.data, big + 1400, 700), "send_x: intact across the send ring's wrap", "");
    peer_send(ACK, peer_seq, g.seq + 700, 8192, 0, 0);
    mn_expect_eq_u16(t.snd_len, 0, "retired");
#undef XBUF
#undef XSEG
#undef XOFF

    /* --- a listener: passive open, data both ways, peer closes --- */
    fresh();
    mn_expect(mn_tcp_listen(&socks[1], 6400), "listen on 6400", "");
    mn_expect_eq_u16(socks[1].state, MN_TCP_LISTEN, "LISTEN");
    our_port = 6400; peer_port = 5555;
    peer_send(SYN, peer_seq, 0, 8192, 0, 0);
    mn_expect_eq_u16(socks[1].state, MN_TCP_SYN_RCVD, "SYN -> SYN_RCVD");
    mn_expect(sent_seg(0, &g) && g.flags == (SYN | ACK) && g.ack == peer_seq + 1, "SYN|ACK sent", "");
    mn_expect_eq_u16(socks[1].peer_mss, 1400, "peer MSS taken from the SYN");
    {
        uint16_t len; const uint8_t *f = stub_link_sent(&s, (uint8_t)(s.tx_count - 1), &len);
        mn_expect_eq_u16(mn_get16(f + 34 + 22), MN_TCP_MSS, "our MSS offered");
    }
    peer_seq++;
    peer_send(ACK, peer_seq, g.seq + 1, 8192, 0, 0);
    mn_expect_eq_u16(socks[1].state, MN_TCP_ESTABLISHED, "handshake ACK -> ESTABLISHED");
    mn_expect_eq_u16(mn_tcp_available(&socks[1]), 0, "nothing yet");
    peer_send(ACK | PSH, peer_seq, socks[1].snd_nxt, 8192, req, 3);
    mn_expect_eq_u16(mn_tcp_available(&socks[1]), 3, "data lands on the accepted socket");
    peer_seq += 3;
    mn_expect_eq_u16(mn_tcp_recv(&socks[1], buf, 10), 3, "and reads back");
    mn_expect_eq_u16(mn_tcp_send(&socks[1], big, 50), 50, "reply queued");
    tick(1);
    mn_expect(sent_seg(0, &g) && g.dlen == 50 && g.ack == peer_seq, "reply sent with the ACK", "");
    peer_send(ACK, peer_seq, g.seq + 50, 8192, 0, 0);
    peer_send(ACK | FIN, peer_seq, socks[1].snd_nxt, 8192, 0, 0); peer_seq++;
    mn_expect_eq_u16(socks[1].state, MN_TCP_CLOSE_WAIT, "peer FIN -> CLOSE_WAIT");
    mn_tcp_close(&socks[1]); tick(1);
    mn_expect_eq_u16(socks[1].state, MN_TCP_LAST_ACK, "our FIN -> LAST_ACK");
    sent_seg(0, &g);
    peer_send(ACK, peer_seq, g.seq + 1, 8192, 0, 0);
    mn_expect_eq_u16(socks[1].state, MN_TCP_CLOSED, "CLOSED: listen again to take another");
    mn_expect(mn_tcp_listen(&socks[1], 6400), "listen again", "");

    /* --- nobody's segments are answered with RST --- */
    fresh();
    our_port = 6401; peer_port = 5556;
    before = s.tx_count;
    peer_send(SYN, peer_seq, 0, 8192, 0, 0);
    mn_expect(s.tx_count == before + 1 && sent_seg(0, &g) && (g.flags & RST) && (g.flags & ACK)
              && g.ack == peer_seq + 1 && g.seq == 0, "SYN to a port with no listener: RST|ACK", "");
    mn_expect_eq_u16(set.rst_sent, 1, "counted");
    peer_send(ACK | PSH, peer_seq, 0x5000, 8192, req, 3);
    mn_expect(sent_seg(0, &g) && g.flags == RST && g.seq == 0x5000, "stray ACK: RST with its ack as seq", "");
    before = s.tx_count;
    peer_send(RST, peer_seq, 0, 0, 0, 0);
    mn_expect_eq_u16(s.tx_count, before, "a stray RST is not answered");

    /* --- two listeners on one port, then a third caller is refused --- */
    fresh();
    mn_tcp_listen(&socks[1], 6400);
    mn_tcp_listen(&socks[2], 6400);
    our_port = 6400;
    peer_port = 5555; peer_send(SYN, 0x100, 0, 8192, 0, 0);
    mn_expect_eq_u16(socks[1].state, MN_TCP_SYN_RCVD, "first SYN takes socket 1");
    sent_seg(0, &g); peer_send(ACK, 0x101, g.seq + 1, 8192, 0, 0);
    peer_port = 5556; peer_send(SYN, 0x200, 0, 8192, 0, 0);
    mn_expect_eq_u16(socks[2].state, MN_TCP_SYN_RCVD, "second SYN takes socket 2");
    sent_seg(0, &g); peer_send(ACK, 0x201, g.seq + 1, 8192, 0, 0);
    mn_expect(socks[1].state == MN_TCP_ESTABLISHED && socks[2].state == MN_TCP_ESTABLISHED,
              "both established", "");
    peer_port = 5555; peer_send(ACK | PSH, 0x101, socks[1].snd_nxt, 8192, big, 10);
    peer_port = 5556; peer_send(ACK | PSH, 0x201, socks[2].snd_nxt, 8192, big + 100, 20);
    mn_expect(mn_tcp_available(&socks[1]) == 10 && mn_tcp_available(&socks[2]) == 20,
              "data goes to the socket that owns the four-tuple", "");
    mn_tcp_recv(&socks[2], buf, 20);
    mn_expect_eq_bytes(buf, big + 100, 20, "socket 2's bytes");
    before = s.tx_count;
    peer_port = 5557; peer_send(SYN, 0x300, 0, 8192, 0, 0);
    mn_expect(sent_seg(0, &g) && (g.flags & RST), "third caller: no free listener, RST", "");
    mn_expect(socks[0].state == MN_TCP_CLOSED && socks[3].state == MN_TCP_CLOSED, "others untouched", "");

    /* --- a client and a server at once --- */
    fresh();
    mn_expect(handshake(), "socket 0 connects out", "");
    mn_tcp_listen(&socks[1], 6400);
    {
        uint16_t client_port = our_port, client_peer = peer_port;
        our_port = 6400; peer_port = 5555;
        peer_send(SYN, 0x100, 0, 8192, 0, 0);
        sent_seg(0, &g); peer_send(ACK, 0x101, g.seq + 1, 8192, 0, 0);
        mn_expect_eq_u16(socks[1].state, MN_TCP_ESTABLISHED, "socket 1 accepts");
        peer_send(ACK | PSH, 0x101, socks[1].snd_nxt, 8192, big, 7);
        our_port = client_port; peer_port = client_peer;
        peer_send(ACK | PSH, peer_seq, t.snd_nxt, 8192, big + 50, 9);
        mn_expect(mn_tcp_available(&t) == 9 && mn_tcp_available(&socks[1]) == 7,
                  "each socket has its own bytes", "");
        mn_tcp_recv(&t, buf, 9);
        mn_expect_eq_bytes(buf, big + 50, 9, "client's bytes");
    }

    /* --- half-open peers: SYN_RCVD times out or is reset back to LISTEN --- */
    fresh();
    mn_tcp_listen(&socks[1], 6400);
    our_port = 6400; peer_port = 5555;
    peer_send(SYN, 0x100, 0, 8192, 0, 0);
    before = s.tx_count;
    tick(60);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 1), "SYN|ACK resent after the RTO");
    for (i = 0; i < 40; i++) tick(100);
    mn_expect_eq_u16(socks[1].state, MN_TCP_LISTEN, "gives up and listens again");
    peer_send(SYN, 0x100, 0, 8192, 0, 0);
    mn_expect_eq_u16(socks[1].state, MN_TCP_SYN_RCVD, "takes the next SYN");
    peer_send(RST, 0x101, 0, 0, 0, 0);
    mn_expect_eq_u16(socks[1].state, MN_TCP_LISTEN, "RST in SYN_RCVD -> LISTEN");
    mn_tcp_close(&socks[1]);
    mn_expect_eq_u16(socks[1].state, MN_TCP_CLOSED, "close on LISTEN -> CLOSED");

    /* --- the pool must not cross a 64 KB boundary: fewer sockets, not garbage --- */
    {
        mn_tcp_set s2; mn_tcp socks2[4];
        uint8_t got = mn_tcp_init(&s2, socks2, 4, &net, MN_XMEM_BASE + 0xE000UL);
        mn_expect_eq_u16(got, 1, "8 KB left in the segment: one socket");
        mn_expect_eq_u16(s2.n, 1, "the set says so too");
        got = mn_tcp_init(&s2, socks2, 4, &net, MN_XMEM_BASE);
        mn_expect_eq_u16(got, 4, "an aligned base takes them all");
    }

    /* ===== the windowed sender (5.17) ===== */
    fresh();
    handshake();                                  /* peer MSS 1400, window 8192 */
    mn_expect_eq_u16(t.cwnd, 2800, "congestion window opens at two segments");
    for (i = 0; i < 3000; i++) big[i] = (uint8_t)(i * 11 + 3);
    mn_expect_eq_u16(mn_tcp_send(&t, big, 3000), 3000, "3000 bytes queued (3 KB ring)");
    before = s.tx_count;
    tick(1);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 2), "two segments go out");
    mn_expect_eq_u16(t.inflight, 2800, "2800 bytes in flight: the window, not the data, is the limit");
    sent_seg(1, &g); mn_expect(g.dlen == 1400 && g.seq == t.snd_una && !memcmp(g.data, big, 1400), "first: bytes 0-1399", "");
    sent_seg(0, &g); mn_expect(g.dlen == 1400 && g.seq == t.snd_una + 1400 && !memcmp(g.data, big + 1400, 1400), "second: bytes 1400-2799", "");
    tick(1);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 2), "nothing more until an ACK");
    peer_send(ACK, peer_seq, t.snd_una + 1400, 8192, 0, 0);
    mn_expect_eq_u16(t.cwnd, 4200, "window grows by a segment per ACK");
    tick(1);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 3), "the ACK releases the rest");
    sent_seg(0, &g); mn_expect(g.dlen == 200 && !memcmp(g.data, big + 2800, 200), "third: the last 200 bytes", "");
    mn_expect_eq_u16(t.inflight, 1600, "1600 in flight");
    peer_send(ACK, peer_seq, t.snd_una + 1600, 8192, 0, 0);
    mn_expect(t.inflight == 0 && t.snd_len == 0, "all acknowledged", "");

    /* retransmission resends the oldest segment only, and shrinks the window */
    mn_tcp_send(&t, big, 2800); tick(1);
    mn_expect_eq_u16(t.inflight, 2800, "two segments out again");
    before = s.tx_count;
    tick(60);
    mn_expect_eq_u16(s.tx_count, (uint16_t)(before + 1), "one retransmission after the RTO");
    sent_seg(0, &g); mn_expect(g.dlen == 1400 && g.seq == t.snd_una, "the oldest segment", "");
    mn_expect_eq_u16(t.cwnd, 1400, "window back to one segment");
    peer_send(ACK, peer_seq, t.snd_una + 2800, 8192, 0, 0);
    mn_expect_eq_u16(t.inflight, 0, "a cumulative ACK retires both");

    /* the peer's window caps what goes out */
    mn_tcp_send(&t, big, 3000);
    peer_send(ACK, peer_seq, t.snd_una, 1000, 0, 0);       /* window 1000 */
    before = s.tx_count; tick(1);
    mn_expect(s.tx_count == before + 1 && sent_seg(0, &g) && g.dlen == 1000, "one 1000-byte segment: the peer's window", "");
    peer_send(ACK, peer_seq, t.snd_una + 1000, 8192, 0, 0);
    tick(1);
    mn_expect_eq_u16(t.inflight, 2000, "window reopened: the rest goes, 1400 + 600");
}
