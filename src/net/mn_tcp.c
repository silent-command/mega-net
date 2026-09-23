#include "mn_tcp.h"
#include "mn_xmem.h"
#include "mn_byteorder.h"
#include "mn_checksum.h"
#include "mn_ipv4.h"

#define HDR_LEN 20
#define FL_FIN 0x01
#define FL_SYN 0x02
#define FL_RST 0x04
#define FL_PSH 0x08
#define FL_ACK 0x10

#define RTO_INITIAL 50          /* 1 s at 50 Hz */
#define RTO_MAX     400
#define MAX_RETRIES 5
#define TIME_WAIT_TICKS 100     /* 2 s; long enough for a stray FIN */
#define WINUPDATE_GROW 256      /* right edge must move this far */
#define CWND_MAX 4              /* segments in flight, at most (5.17) */

/* Sequence arithmetic: modulo 2^32. */
#define SEQ_LT(a, b)  ((int32_t)((a) - (b)) < 0)
#define SEQ_LEQ(a, b) ((int32_t)((a) - (b)) <= 0)

static uint16_t rcv_free(const mn_tcp *t)
{
    return (uint16_t)(MN_TCP_RCVBUF - t->rcv_count);
}

static uint16_t adv_window(const mn_tcp *t)
{
    uint16_t w = rcv_free(t);
    return w > MN_TCP_WINDOW_CAP ? MN_TCP_WINDOW_CAP : w;
}

/* --- rings in external memory ------------------------------------------
 * Each is `size` bytes at offset `base` in segment `seg`; a position
 * wraps. At most two copies. */
static MN_NOINLINE void ring_read(uint8_t *dst, uint16_t seg, uint16_t base, uint16_t size,
                                  uint16_t pos, uint16_t len)
{
    uint16_t first = (uint16_t)(size - pos);
    if (first > len) first = len;
    mn_xmem_read(dst, seg, (uint16_t)(base + pos), first);
    if (len > first) mn_xmem_read(dst + first, seg, base, (uint16_t)(len - first));
}

static MN_NOINLINE void ring_write(uint16_t seg, uint16_t base, uint16_t size, uint16_t pos,
                                   const uint8_t *src, uint16_t len)
{
    uint16_t first = (uint16_t)(size - pos);
    if (first > len) first = len;
    mn_xmem_write(seg, (uint16_t)(base + pos), src, first);
    if (len > first) mn_xmem_write(seg, base, src + first, (uint16_t)(len - first));
}

static MN_NOINLINE void ring_read_x(uint32_t dst, uint16_t seg, uint16_t base, uint16_t size,
                                    uint16_t pos, uint16_t len)
{
    uint16_t first = (uint16_t)(size - pos);
    if (first > len) first = len;
    mn_xmem_copy_out(dst, seg, (uint16_t)(base + pos), first);
    if (len > first) mn_xmem_copy_out(dst + first, seg, base, (uint16_t)(len - first));
}

static MN_NOINLINE void ring_write_x(uint16_t seg, uint16_t base, uint16_t size, uint16_t pos,
                                     uint32_t src, uint16_t len)
{
    uint16_t first = (uint16_t)(size - pos);
    if (first > len) first = len;
    mn_xmem_copy_in(seg, (uint16_t)(base + pos), src, first);
    if (len > first) mn_xmem_copy_in(seg, base, src + first, (uint16_t)(len - first));
}

/* Builds and sends one segment carrying flags, and len bytes of the send
 * ring starting `off` bytes past its head (off = 0: the oldest
 * unacknowledged data) when len > 0. Returns MN_SEND_*. */
static MN_NOINLINE uint8_t emit(mn_tcp *t, mn_net *net, uint8_t flags, uint32_t seq,
                    uint16_t off, uint16_t len, uint8_t with_mss)
{
    uint8_t *seg, why;
    uint16_t hlen = HDR_LEN, w;

    seg = mn_net_tx_begin(net, t->remote_ip, &why);
    if (!seg)
        return why;
    if (with_mss)
        hlen = HDR_LEN + 4;

    mn_put16(seg + 0, t->local_port);
    mn_put16(seg + 2, t->remote_port);
    mn_put32(seg + 4, seq);
    mn_put32(seg + 8, (flags & FL_ACK) ? t->rcv_nxt : 0);
    seg[12] = (uint8_t)((hlen / 4) << 4);
    seg[13] = flags;
    w = adv_window(t);
    mn_put16(seg + 14, w);
    mn_put16(seg + 16, 0);
    mn_put16(seg + 18, 0);
    if (with_mss) {
        seg[20] = 2; seg[21] = 4;
        mn_put16(seg + 22, MN_TCP_MSS);
    }
    if (len)
        ring_read(seg + hlen, t->xseg, t->sndbuf, MN_TCP_SNDBUF,
                  (uint16_t)((t->snd_head + off) % MN_TCP_SNDBUF), len);
    mn_put16(seg + 16, mn_checksum_pseudo(net->ip, t->remote_ip, MN_IPPROTO_TCP,
                                          seg, (uint16_t)(hlen + len)));
    if (flags & FL_ACK)
        t->adv_edge = t->rcv_nxt + w;
    t->stat_tx_seg++;
    return mn_net_tx_send(net, t->remote_ip, MN_IPPROTO_TCP, (uint16_t)(hlen + len));
}

/* A segment nobody owns is answered with RST, so a peer talking to a
 * port with no listener, or to a connection we have forgotten, learns
 * it at once instead of retrying for a minute. */
static MN_NOINLINE void send_rst(mn_net *net, const uint8_t *ip, const uint8_t *seg,
                     uint8_t flags, uint32_t seq, uint32_t ack, uint16_t dlen)
{
    uint8_t *out, why;
    uint32_t oseq = 0, oack = 0;
    uint8_t oflags = FL_RST;

    if (flags & FL_ACK) {
        oseq = ack;
    } else {
        oack = seq + dlen + ((flags & FL_SYN) ? 1 : 0) + ((flags & FL_FIN) ? 1 : 0);
        oflags |= FL_ACK;
    }
    out = mn_net_tx_begin(net, mn_ipv4_src(ip), &why);
    if (!out)
        return;
    mn_put16(out + 0, mn_get16(seg + 2));
    mn_put16(out + 2, mn_get16(seg));
    mn_put32(out + 4, oseq);
    mn_put32(out + 8, oack);
    out[12] = (uint8_t)((HDR_LEN / 4) << 4);
    out[13] = oflags;
    mn_put16(out + 14, 0);
    mn_put16(out + 16, 0);
    mn_put16(out + 18, 0);
    mn_put16(out + 16, mn_checksum_pseudo(net->ip, mn_ipv4_src(ip), MN_IPPROTO_TCP, out, HDR_LEN));
    mn_net_tx_send(net, mn_ipv4_src(ip), MN_IPPROTO_TCP, HDR_LEN);
}

static MN_NOINLINE void reset_counters(mn_tcp *t)
{
    t->flags = 0;
    t->snd_head = t->snd_len = t->inflight = 0;
    t->cwnd = 0;
    t->rcv_head = t->rcv_count = 0;
    t->fin_queued = t->fin_sent = 0;
    t->ack_pending = t->syn_pending = t->tx_pending = 0;
    t->adv_edge = 0;
    t->snd_wnd = 0;
    t->peer_mss = 536;
    t->rto = RTO_INITIAL;
    t->retries = 0;
}

uint8_t mn_tcp_init(mn_tcp_set *set, mn_tcp *socks, uint8_t n, mn_net *net,
                    uint32_t xmem_base)
{
    uint8_t i;
    /* Ring arithmetic is 16-bit within one segment: only as many sockets
     * as fit before the next 64 KB boundary. */
    uint32_t room = 0x10000UL - (xmem_base & 0xFFFFUL);
    if ((uint32_t)n * MN_TCP_SOCKET_XMEM > room)
        n = (uint8_t)(room / MN_TCP_SOCKET_XMEM);

    set->sock = socks;
    set->n = n;
    set->rst_sent = 0;
    set->now = 0;
    for (i = 0; i < n; i++) {
        mn_tcp *t = &socks[i];
        t->state = MN_TCP_CLOSED;
        t->index = i;
        t->xseg = (uint16_t)(xmem_base >> 16);
        t->rcvbuf = (uint16_t)(xmem_base + (uint32_t)i * MN_TCP_SOCKET_XMEM);
        t->sndbuf = (uint16_t)(t->rcvbuf + MN_TCP_RCVBUF);
        t->local_port = t->remote_port = 0;
        reset_counters(t);
        t->stat_rx_seg = t->stat_tx_seg = t->stat_retrans = t->stat_ooo = 0;
    }
    net->tcp_input = mn_tcp_input;
    net->tcp_ctx = set;
    return n;
}

static uint32_t initial_seq(const mn_net *net, uint16_t now, uint8_t index)
{
    return ((uint32_t)now << 16) | ((uint32_t)net->nif->mac[4] << 8) |
           (uint32_t)(uint8_t)(net->nif->mac[5] + index * 61);
}

uint8_t mn_tcp_connect(mn_tcp *t, mn_net *net, const uint8_t *ip,
                       uint16_t port, uint16_t now)
{
    uint8_t i;

    if (t->state != MN_TCP_CLOSED)
        return 0;
    for (i = 0; i < 4; i++)
        t->remote_ip[i] = ip[i];
    t->remote_port = port;
    t->local_port = (uint16_t)(49152 + ((now * 3 + net->nif->mac[5] + t->index * 37) & 0x0fff));
    t->iss = initial_seq(net, now, t->index);
    t->snd_una = t->iss;
    t->snd_nxt = t->iss + 1;
    t->rcv_nxt = 0;
    reset_counters(t);
    t->state = MN_TCP_SYN_SENT;
    t->sent_at = now;
    t->syn_pending = (uint8_t)(emit(t, net, FL_SYN, t->iss, 0, 0, 1) != MN_SEND_OK);
    return 1;
}

uint8_t mn_tcp_listen(mn_tcp *t, uint16_t port)
{
    if (t->state != MN_TCP_CLOSED || port == 0)
        return 0;
    t->local_port = port;
    t->remote_port = 0;
    reset_counters(t);
    t->state = MN_TCP_LISTEN;
    return 1;
}

static uint8_t can_send(const mn_tcp *t)
{
    return (t->state == MN_TCP_ESTABLISHED || t->state == MN_TCP_CLOSE_WAIT) && !t->fin_queued;
}

uint16_t mn_tcp_send(mn_tcp *t, const uint8_t *data, uint16_t len)
{
    uint16_t room;

    if (!can_send(t))
        return 0;
    room = (uint16_t)(MN_TCP_SNDBUF - t->snd_len);
    if (len > room)
        len = room;
    if (len) {
        ring_write(t->xseg, t->sndbuf, MN_TCP_SNDBUF,
                   (uint16_t)((t->snd_head + t->snd_len) % MN_TCP_SNDBUF), data, len);
        t->snd_len = (uint16_t)(t->snd_len + len);
        t->tx_pending = 1;
    }
    return len;
}

uint16_t mn_tcp_send_x(mn_tcp *t, uint32_t src, uint16_t len)
{
    uint16_t room;

    if (!can_send(t))
        return 0;
    room = (uint16_t)(MN_TCP_SNDBUF - t->snd_len);
    if (len > room)
        len = room;
    if (len) {
        ring_write_x(t->xseg, t->sndbuf, MN_TCP_SNDBUF,
                     (uint16_t)((t->snd_head + t->snd_len) % MN_TCP_SNDBUF), src, len);
        t->snd_len = (uint16_t)(t->snd_len + len);
        t->tx_pending = 1;
    }
    return len;
}

uint16_t mn_tcp_available(const mn_tcp *t)
{
    return t->rcv_count;
}

/* After bytes leave the ring: the right edge moved, so tell the peer once
 * it has moved enough, or if the window had closed entirely. */
static MN_NOINLINE void after_recv(mn_tcp *t, uint16_t n)
{
    t->rcv_head = (uint16_t)((t->rcv_head + n) % MN_TCP_RCVBUF);
    t->rcv_count = (uint16_t)(t->rcv_count - n);
    if (n && (t->state == MN_TCP_ESTABLISHED || t->state == MN_TCP_FIN_WAIT_1 ||
              t->state == MN_TCP_FIN_WAIT_2)) {
        uint32_t edge = t->rcv_nxt + adv_window(t);
        if (edge - t->adv_edge >= WINUPDATE_GROW || t->adv_edge == t->rcv_nxt)
            t->ack_pending = 1;
    }
}

uint16_t mn_tcp_recv(mn_tcp *t, uint8_t *out, uint16_t cap)
{
    uint16_t n = t->rcv_count;
    if (n > cap)
        n = cap;
    if (n)
        ring_read(out, t->xseg, t->rcvbuf, MN_TCP_RCVBUF, t->rcv_head, n);
    after_recv(t, n);
    return n;
}

uint16_t mn_tcp_recv_x(mn_tcp *t, uint32_t dst, uint16_t cap)
{
    uint16_t n = t->rcv_count;
    if (n > cap)
        n = cap;
    if (n)
        ring_read_x(dst, t->xseg, t->rcvbuf, MN_TCP_RCVBUF, t->rcv_head, n);
    after_recv(t, n);
    return n;
}

void mn_tcp_close(mn_tcp *t)
{
    if (t->state == MN_TCP_ESTABLISHED || t->state == MN_TCP_CLOSE_WAIT) {
        t->fin_queued = 1;
        t->tx_pending = 1;
    } else if (t->state == MN_TCP_SYN_SENT || t->state == MN_TCP_LISTEN ||
               t->state == MN_TCP_SYN_RCVD) {
        /* A half-open peer's retransmissions now belong to nobody and are
         * answered with RST by the dispatcher. */
        t->state = MN_TCP_CLOSED;
    }
}

void mn_tcp_abort(mn_tcp *t, mn_net *net)
{
    if (t->state != MN_TCP_CLOSED && t->state != MN_TCP_TIME_WAIT &&
        t->state != MN_TCP_LISTEN)
        emit(t, net, FL_RST | FL_ACK, t->snd_nxt, 0, 0, 0);
    t->state = MN_TCP_CLOSED;
    t->snd_head = t->snd_len = t->inflight = 0;
    t->fin_queued = t->fin_sent = 0;
    t->ack_pending = t->syn_pending = t->tx_pending = 0;
}

/* Appends a segment's payload to the ring, as much as fits. */
static MN_NOINLINE uint16_t ring_put(mn_tcp *t, const uint8_t *data, uint16_t len)
{
    uint16_t free = rcv_free(t);
    if (len > free)
        len = free;
    if (len)
        ring_write(t->xseg, t->rcvbuf, MN_TCP_RCVBUF,
                   (uint16_t)((t->rcv_head + t->rcv_count) % MN_TCP_RCVBUF), data, len);
    t->rcv_count = (uint16_t)(t->rcv_count + len);
    return len;
}

/* The peer acknowledged up to ack: retire sent data. */
static MN_NOINLINE void handle_ack(mn_tcp *t, uint32_t ack, uint16_t now)
{
    uint16_t acked;

    if (SEQ_LEQ(ack, t->snd_una) || SEQ_LT(t->snd_nxt, ack))
        return;                                   /* old, or beyond what we sent */
    acked = (uint16_t)(ack - t->snd_una);
    if (t->fin_sent && ack == t->snd_nxt)         /* the FIN itself */
        acked = (uint16_t)(acked - 1);
    if (acked > t->inflight)
        acked = t->inflight;
    t->snd_head = (uint16_t)((t->snd_head + acked) % MN_TCP_SNDBUF);
    t->snd_len = (uint16_t)(t->snd_len - acked);
    t->inflight = (uint16_t)(t->inflight - acked);
    t->snd_una = ack;
    t->retries = 0;
    t->rto = RTO_INITIAL;
    t->sent_at = now;                             /* the timer covers the oldest in flight */
    /* One more segment's worth per acknowledgement, up to CWND_MAX segments. */
    if (t->cwnd < (uint16_t)(t->peer_mss * CWND_MAX))
        t->cwnd = (uint16_t)(t->cwnd + t->peer_mss);
    if (t->snd_len > t->inflight)
        t->tx_pending = 1;
}

/* The MSS option of a SYN, if offered. */
static MN_NOINLINE void take_mss(mn_tcp *t, const uint8_t *seg, uint16_t hlen)
{
    uint16_t o;
    for (o = HDR_LEN; o + 1 < hlen;) {
        uint8_t kind = seg[o];
        if (kind == 0) break;
        if (kind == 1) { o++; continue; }
        if (kind == 2 && seg[o + 1] == 4 && o + 3 < hlen)
            t->peer_mss = mn_get16(seg + o + 2);
        if (seg[o + 1] < 2) break;
        o = (uint16_t)(o + seg[o + 1]);
    }
    if (t->peer_mss > MN_TCP_MSS) t->peer_mss = MN_TCP_MSS;
}

/* A SYN for a listening socket: the passive open. */
static MN_NOINLINE void passive_open(mn_tcp_set *set, mn_tcp *t, mn_net *net, const uint8_t *ip,
                         const uint8_t *seg, uint16_t hlen, uint32_t seq)
{
    uint8_t i;
    const uint8_t *src = mn_ipv4_src(ip);

    for (i = 0; i < 4; i++)
        t->remote_ip[i] = src[i];
    t->remote_port = mn_get16(seg);
    reset_counters(t);
    t->irs = seq;
    t->rcv_nxt = seq + 1;
    t->iss = initial_seq(net, set->now, t->index);
    t->snd_una = t->iss;
    t->snd_nxt = t->iss + 1;
    t->snd_wnd = mn_get16(seg + 14);
    take_mss(t, seg, hlen);
    t->state = MN_TCP_SYN_RCVD;
    t->sent_at = set->now;
    t->syn_pending = (uint8_t)(emit(t, net, FL_SYN | FL_ACK, t->iss, 0, 0, 1) != MN_SEND_OK);
}

/* One segment for one socket that owns it. */
static MN_NOINLINE void segment_input(mn_tcp *t, mn_net *net, const uint8_t *seg, uint16_t hlen,
                          uint8_t flags, uint32_t seq, uint32_t ack, uint16_t dlen, uint16_t now)
{
    uint16_t i;

    t->stat_rx_seg++;

    if (flags & FL_RST) {
        if (t->state == MN_TCP_SYN_SENT) {
            if ((flags & FL_ACK) && ack == t->snd_nxt) {
                t->flags |= MN_TCP_F_REFUSED | MN_TCP_F_RESET;
                t->state = MN_TCP_CLOSED;
            }
            return;
        }
        if (t->state == MN_TCP_SYN_RCVD) {
            t->state = MN_TCP_LISTEN;             /* the peer gave up: wait again */
            t->flags = 0;
            return;
        }
        if (SEQ_LT(seq, t->rcv_nxt) || SEQ_LEQ(t->rcv_nxt + adv_window(t), seq))
            return;                               /* outside the window: ignore */
        t->flags |= MN_TCP_F_RESET;
        t->state = MN_TCP_CLOSED;
        return;
    }

    if (t->state == MN_TCP_SYN_SENT) {
        if (!(flags & FL_SYN) || !(flags & FL_ACK) || ack != t->snd_nxt)
            return;
        t->irs = seq;
        t->rcv_nxt = seq + 1;
        t->snd_una = ack;
        t->snd_wnd = mn_get16(seg + 14);
        take_mss(t, seg, hlen);
        t->state = MN_TCP_ESTABLISHED;
        t->cwnd = (uint16_t)(t->peer_mss * 2);
        t->retries = 0;
        t->rto = RTO_INITIAL;
        t->ack_pending = 1;
        return;
    }

    if (t->state == MN_TCP_SYN_RCVD) {
        if (flags & FL_SYN) {                     /* our SYN|ACK was lost: again */
            if (seq + 1 == t->rcv_nxt)
                t->syn_pending = 1;
            return;
        }
        if (!(flags & FL_ACK) || ack != t->snd_nxt || seq != t->rcv_nxt)
            return;
        t->snd_una = ack;
        t->state = MN_TCP_ESTABLISHED;
        t->cwnd = (uint16_t)(t->peer_mss * 2);
        t->retries = 0;
        t->rto = RTO_INITIAL;
        /* The handshake's last ACK may carry data: fall through. */
    }

    if (!(flags & FL_ACK))
        return;

    /* Only in-order data is taken; anything else is answered with what
     * we expect, which is how the peer learns to resend it. */
    if (seq != t->rcv_nxt) {
        if (dlen || (flags & FL_FIN)) {
            t->stat_ooo++;
            t->ack_pending = 1;
        }
        handle_ack(t, ack, now);
        return;
    }

    handle_ack(t, ack, now);
    t->snd_wnd = mn_get16(seg + 14);

    if (dlen) {
        if (t->state == MN_TCP_ESTABLISHED || t->state == MN_TCP_FIN_WAIT_1 ||
            t->state == MN_TCP_FIN_WAIT_2) {
            i = ring_put(t, seg + hlen, dlen);
            t->rcv_nxt += i;
            if (i < dlen)
                flags &= (uint8_t)~FL_FIN;        /* FIN not yet reached */
        }
        t->ack_pending = 1;
    }

    if (flags & FL_FIN) {
        t->rcv_nxt++;
        t->flags |= MN_TCP_F_EOF;
        t->ack_pending = 1;
        switch (t->state) {
        case MN_TCP_ESTABLISHED: t->state = MN_TCP_CLOSE_WAIT; break;
        case MN_TCP_FIN_WAIT_1:  t->state = MN_TCP_CLOSING; break;
        case MN_TCP_FIN_WAIT_2:  t->state = MN_TCP_TIME_WAIT; break;
        default: break;
        }
    }

    /* Our FIN acknowledged? */
    if (t->fin_sent && t->snd_una == t->snd_nxt) {
        switch (t->state) {
        case MN_TCP_FIN_WAIT_1: t->state = MN_TCP_FIN_WAIT_2; break;
        case MN_TCP_CLOSING:    t->state = MN_TCP_TIME_WAIT; break;
        case MN_TCP_LAST_ACK:   t->state = MN_TCP_CLOSED; break;
        default: break;
        }
    }
}

/* The dispatcher: the socket that owns the segment's four-tuple, else a
 * listener for a SYN, else RST. */
void mn_tcp_input(void *ctx, mn_net *net, const uint8_t *ip, uint16_t len)
{
    mn_tcp_set *set = (mn_tcp_set *)ctx;
    const uint8_t *seg = mn_ipv4_payload(ip);
    uint16_t slen = mn_ipv4_payload_len(ip), hlen, dlen, sport, dport;
    uint32_t seq, ack;
    uint8_t flags, i;
    (void)len;

    if (slen < HDR_LEN)
        return;
    if (mn_checksum_pseudo(mn_ipv4_src(ip), mn_ipv4_dst(ip), MN_IPPROTO_TCP, seg, slen) != 0)
        return;
    hlen = (uint16_t)((seg[12] >> 4) * 4);
    if (hlen < HDR_LEN || hlen > slen)
        return;
    sport = mn_get16(seg);
    dport = mn_get16(seg + 2);
    flags = seg[13];
    seq = mn_get32(seg + 4);
    ack = mn_get32(seg + 8);
    dlen = (uint16_t)(slen - hlen);

    for (i = 0; i < set->n; i++) {
        mn_tcp *t = &set->sock[i];
        if (t->state == MN_TCP_CLOSED || t->state == MN_TCP_LISTEN)
            continue;
        if (t->local_port == dport && t->remote_port == sport &&
            mn_ipv4_addr_eq(mn_ipv4_src(ip), t->remote_ip)) {
            segment_input(t, net, seg, hlen, flags, seq, ack, dlen, set->now);
            return;
        }
    }
    if ((flags & (FL_SYN | FL_ACK | FL_RST)) == FL_SYN) {
        for (i = 0; i < set->n; i++) {
            mn_tcp *t = &set->sock[i];
            if (t->state == MN_TCP_LISTEN && t->local_port == dport) {
                passive_open(set, t, net, ip, seg, hlen, seq);
                return;
            }
        }
    }
    if (!(flags & FL_RST)) {
        send_rst(net, ip, seg, flags, seq, ack, dlen);
        set->rst_sent++;
    }
}

static MN_NOINLINE void poll_one(mn_tcp *t, mn_net *net, uint16_t now)
{
    uint16_t n;
    uint8_t r;

    switch (t->state) {
    case MN_TCP_CLOSED:
    case MN_TCP_LISTEN:
        return;

    case MN_TCP_SYN_SENT:
    case MN_TCP_SYN_RCVD:
        if (t->syn_pending || (uint16_t)(now - t->sent_at) >= t->rto) {
            if (!t->syn_pending) {
                if (++t->retries > MAX_RETRIES) {
                    if (t->state == MN_TCP_SYN_RCVD) {
                        t->state = MN_TCP_LISTEN;  /* the peer never finished: wait again */
                        t->flags = 0;
                    } else {
                        t->flags |= MN_TCP_F_TIMEOUT;
                        t->state = MN_TCP_CLOSED;
                    }
                    return;
                }
                t->stat_retrans++;
                if (t->rto < RTO_MAX) t->rto = (uint16_t)(t->rto * 2);
            }
            t->sent_at = now;
            r = emit(t, net, t->state == MN_TCP_SYN_SENT ? FL_SYN : (FL_SYN | FL_ACK),
                     t->iss, 0, 0, 1);
            t->syn_pending = (uint8_t)(r != MN_SEND_OK);
        }
        return;

    case MN_TCP_TIME_WAIT:
        if (t->ack_pending) {
            t->ack_pending = 0;
            emit(t, net, FL_ACK, t->snd_nxt, 0, 0, 0);
            t->timewait_at = now;
        }
        if ((uint16_t)(now - t->timewait_at) >= TIME_WAIT_TICKS)
            t->state = MN_TCP_CLOSED;
        return;

    default:
        break;
    }

    /* Retransmit if the peer has gone quiet: the oldest segment only, and
     * the window back to one segment. */
    if ((t->inflight || t->fin_sent) && (uint16_t)(now - t->sent_at) >= t->rto) {
        if (++t->retries > MAX_RETRIES) {
            t->flags |= MN_TCP_F_TIMEOUT;
            t->state = MN_TCP_CLOSED;
            return;
        }
        t->stat_retrans++;
        if (t->rto < RTO_MAX) t->rto = (uint16_t)(t->rto * 2);
        t->sent_at = now;
        t->cwnd = t->peer_mss;
        if (t->inflight)
            emit(t, net, FL_ACK | FL_PSH, t->snd_una, 0,
                 t->inflight > t->peer_mss ? t->peer_mss : t->inflight, 0);
        else
            emit(t, net, FL_ACK | FL_FIN, t->snd_nxt - 1, 0, 0, 0);
        return;
    }

    /* Send queued data: as many segments as the peer's window and our own
     * congestion window allow (5.17). */
    if (t->tx_pending && t->snd_len > t->inflight) {
        uint16_t limit = t->snd_wnd < t->cwnd ? t->snd_wnd : t->cwnd;
        r = MN_SEND_OK;
        while (t->snd_len > t->inflight && t->inflight < limit) {
            n = (uint16_t)(t->snd_len - t->inflight);
            if (n > t->peer_mss) n = t->peer_mss;
            if (n > (uint16_t)(limit - t->inflight)) n = (uint16_t)(limit - t->inflight);
            r = emit(t, net, FL_ACK | FL_PSH, t->snd_nxt, t->inflight, n, 0);
            if (r != MN_SEND_OK)
                break;                            /* pending or failed: next poll */
            if (!t->inflight)
                t->sent_at = now;                 /* the timer starts with the first */
            t->inflight = (uint16_t)(t->inflight + n);
            t->snd_nxt += n;
            t->ack_pending = 0;
        }
        t->tx_pending = (uint8_t)(t->snd_len > t->inflight);
        if (r == MN_SEND_PENDING)
            t->tx_pending = 1;
        return;
    }

    /* Queued data all acknowledged and the application wants out: FIN. */
    if (t->fin_queued && !t->fin_sent && !t->inflight && !t->snd_len) {
        r = emit(t, net, FL_ACK | FL_FIN, t->snd_nxt, 0, 0, 0);
        if (r == MN_SEND_OK) {
            t->fin_sent = 1;
            t->snd_nxt++;
            t->sent_at = now;
            t->ack_pending = 0;
            t->state = (t->state == MN_TCP_CLOSE_WAIT) ? MN_TCP_LAST_ACK : MN_TCP_FIN_WAIT_1;
        }
        return;
    }

    if (t->ack_pending) {
        if (emit(t, net, FL_ACK, t->snd_nxt, 0, 0, 0) != MN_SEND_PENDING)
            t->ack_pending = 0;
    }
}

void mn_tcp_poll(mn_tcp_set *set, mn_net *net, uint16_t now)
{
    uint8_t i;
    set->now = now;
    for (i = 0; i < set->n; i++)
        poll_one(&set->sock[i], net, now);
}
