#include "mn_dhcp.h"
#include "mn_byteorder.h"

#define PORT_SERVER 67
#define PORT_CLIENT 68

#define OP_REQUEST 1
#define OP_REPLY   2

#define OPT_MASK      1
#define OPT_ROUTER    3
#define OPT_DNS       6
#define OPT_REQ_IP    50
#define OPT_LEASE     51
#define OPT_MSG_TYPE  53
#define OPT_SERVER_ID 54
#define OPT_PARAM_REQ 55
#define OPT_END       255

#define MSG_DISCOVER 1
#define MSG_OFFER    2
#define MSG_REQUEST  3
#define MSG_ACK      5
#define MSG_NAK      6

#define HDR_LEN    236            /* fixed BOOTP header */
#define COOKIE_LEN 4
#define MIN_LEN    300            /* BOOTP minimum; some servers insist */
#define MAX_MSG    MN_UDP_SOCK_BUF

/* Retransmit after RETRY_TICKS (4 s at 50 Hz), give up after MAX_TRIES. */
#define RETRY_TICKS 200
#define MAX_TRIES   5

/* The tick is the frame counter: 50 Hz on PAL. On NTSC the lease clock
 * runs a fifth fast, which only renews early. */
#define TICKS_PER_S   50
#define RENEW_RETRY_M 1           /* minutes between REQUESTs while renewing or rebinding */
#define LEASE_FOREVER_S 3888000UL /* 45 days: beyond the minute clock, so never renewed */

static const uint8_t bcast[4] = { 255, 255, 255, 255 };
static const uint8_t cookie[4] = { 0x63, 0x82, 0x53, 0x63 };
/* Messages are built and parsed in the network layer's receive buffer,
 * which is idle between polls (5.12: the window had no room for a
 * private one). A message never outlives the poll that handles it. */
#define msg (mn_net_scratch())

static void copy4(uint8_t *d, const uint8_t *s)
{
    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
}

static uint16_t build(mn_dhcp *d, const mn_net *net, uint8_t type)
{
    uint16_t i, o;
    /* A REQUEST while bound (renewing or rebinding) carries our address
     * in ciaddr and neither the requested-address nor the server-id
     * option, and asks for a unicast reply (RFC 2131 4.3.2). */
    uint8_t bound = (uint8_t)(d->state == MN_DHCP_BOUND);

    for (i = 0; i < MIN_LEN; i++)
        msg[i] = 0;
    msg[0] = OP_REQUEST;
    msg[1] = 1;                                   /* htype: ethernet */
    msg[2] = 6;                                   /* hlen */
    for (i = 0; i < 4; i++)
        msg[4 + i] = d->xid[i];
    msg[10] = bound ? 0x00 : 0x80;                /* flags: broadcast reply */
    if (bound)
        copy4(msg + 12, net->ip);                 /* ciaddr */
    for (i = 0; i < 6; i++)
        msg[28 + i] = net->nif->mac[i];           /* chaddr */
    for (i = 0; i < 4; i++)
        msg[HDR_LEN + i] = cookie[i];

    o = HDR_LEN + COOKIE_LEN;
    msg[o++] = OPT_MSG_TYPE; msg[o++] = 1; msg[o++] = type;
    if (type == MSG_REQUEST && !bound) {
        msg[o++] = OPT_REQ_IP;    msg[o++] = 4;
        copy4(msg + o, d->offered_ip); o += 4;
        msg[o++] = OPT_SERVER_ID; msg[o++] = 4;
        copy4(msg + o, d->server_id); o += 4;
    }
    msg[o++] = OPT_PARAM_REQ; msg[o++] = 4;
    msg[o++] = OPT_MASK; msg[o++] = OPT_ROUTER; msg[o++] = OPT_DNS; msg[o++] = OPT_LEASE;
    msg[o++] = OPT_END;
    return o < MIN_LEN ? MIN_LEN : o;
}

static void transmit(mn_dhcp *d, mn_net *net, uint8_t type, uint16_t now)
{
    uint16_t n = build(d, net, type);
    /* Renewing goes to the server that granted the lease; everything else
     * is broadcast. */
    const uint8_t *dst = (d->state == MN_DHCP_BOUND && d->phase == MN_DHCP_PHASE_RENEWING)
                         ? d->server_id : bcast;
    uint8_t r = mn_net_send_udp(net, dst, PORT_CLIENT, PORT_SERVER, msg, n);
    d->pending = (uint8_t)(r == MN_SEND_PENDING);
    d->sent_at = now;
    /* A send the link refused counts as a try too: a link that keeps
     * refusing must lead to FAILED, not to a client that waits forever
     * (found with the gopher port, REQUIREMENTS.md 5.9). */
    if (r != MN_SEND_PENDING)
        d->tries++;
}

void mn_dhcp_start(mn_dhcp *d, mn_net *net, uint16_t now)
{
    uint8_t i;
    static const uint8_t zero[4] = { 0, 0, 0, 0 };

    /* Unconfigured while acquiring: source 0.0.0.0, and the IPv4 layer
     * accepts anything addressed to us by MAC. */
    mn_net_set_ip(net, zero, zero, zero);

    if (d->sock != 0xff)
        mn_net_udp_close(net, d->sock);
    d->sock = mn_net_udp_open(net, PORT_CLIENT);
    /* Transaction id from the MAC and the clock: distinct enough. */
    d->xid[0] = net->nif->mac[3]; d->xid[1] = net->nif->mac[4];
    d->xid[2] = net->nif->mac[5]; d->xid[3] = (uint8_t)now;
    for (i = 0; i < 4; i++)
        d->offered_ip[i] = d->server_id[i] = d->mask[i] = d->gw[i] = d->dns[i] = 0;
    d->lease_s = 0;
    d->phase = MN_DHCP_PHASE_BOUND;
    d->age_m = d->t1_m = d->t2_m = d->next_m = d->lease_m = 0;
    d->tick_last = now; d->tick_acc = 0; d->sec_acc = 0;
    d->tries = 0;
    d->rx_count = 0;
    d->last_len = 0; d->last_op = 0; d->last_type = 0;
    d->state = MN_DHCP_SELECTING;
    transmit(d, net, MSG_DISCOVER, now);
}

#define OPT_OVERLOAD 52
#define OFF_SNAME    44
#define OFF_FILE     108

/* Walks one option area, m[o..end). Fills what it finds; returns via
 * pointers the message type and the overload flags, if present. */
static void walk(mn_dhcp *d, const uint8_t *m, uint16_t o, uint16_t end,
                 uint8_t *type, uint8_t *overload)
{
    uint8_t code, olen;

    while (o < end) {
        code = m[o++];
        if (code == OPT_END)
            break;
        if (code == 0)
            continue;                             /* pad */
        if (o >= end)
            break;
        olen = m[o++];
        if ((uint16_t)(o + olen) > end)
            break;
        switch (code) {
        case OPT_MSG_TYPE:  if (olen >= 1) *type = m[o]; break;
        case OPT_OVERLOAD:  if (olen >= 1) *overload = m[o]; break;
        case OPT_MASK:      if (olen >= 4) copy4(d->mask, m + o); break;
        case OPT_ROUTER:    if (olen >= 4) copy4(d->gw, m + o); break;
        case OPT_DNS:       if (olen >= 4) copy4(d->dns, m + o); break;
        case OPT_SERVER_ID: if (olen >= 4) copy4(d->server_id, m + o); break;
        case OPT_LEASE:     if (olen >= 4) d->lease_s = mn_get32(m + o); break;
        default: break;
        }
        o = (uint16_t)(o + olen);
    }
}

/* Reads a received message. Returns the message type, or 0 if the
 * message is not for us. Fills the lease details it finds. Honours
 * option 52 (overload): a server may continue its options in the
 * `file` field (bit 0) and/or the `sname` field (bit 1) -- a FiOS
 * gateway puts the message type itself there (REQUIREMENTS.md 5.7). */
static uint8_t parse(mn_dhcp *d, const mn_net *net, const uint8_t *m,
                     uint16_t len, uint8_t *yiaddr)
{
    uint8_t i, type = 0, overload = 0;

    if (len < HDR_LEN + COOKIE_LEN || m[0] != OP_REPLY)
        return 0;
    for (i = 0; i < 4; i++)
        if (m[4 + i] != d->xid[i])
            return 0;
    for (i = 0; i < 6; i++)
        if (m[28 + i] != net->nif->mac[i])
            return 0;
    for (i = 0; i < 4; i++)
        if (m[HDR_LEN + i] != cookie[i])
            return 0;
    copy4(yiaddr, m + 16);

    walk(d, m, HDR_LEN + COOKIE_LEN, len, &type, &overload);
    if (overload & 1)
        walk(d, m, OFF_FILE, HDR_LEN, &type, &overload);
    if (overload & 2)
        walk(d, m, OFF_SNAME, OFF_FILE, &type, &overload);
    return type;
}

/* The lease was granted or renewed: set the clocks. */
static void granted(mn_dhcp *d, uint16_t now)
{
    d->phase = MN_DHCP_PHASE_BOUND;
    d->age_m = 0;
    d->tick_last = now; d->tick_acc = 0; d->sec_acc = 0;
    if (d->lease_s < 120 || d->lease_s >= LEASE_FOREVER_S) {
        /* BOOTP (0), forever, or too short to bother: never renewed. */
        d->lease_m = d->t1_m = d->t2_m = 0;
    } else {
        d->lease_m = (uint16_t)(d->lease_s / 60);
        d->t1_m = (uint16_t)(d->lease_m / 2);
        d->t2_m = (uint16_t)(d->lease_m - d->lease_m / 8);
    }
    d->next_m = d->t1_m;
}

/* Advances the minute clock from the tick. Polled at least once every
 * 21 minutes (the 16-bit tick's wrap) it never loses time. */
static void clock_tick(mn_dhcp *d, uint16_t now)
{
    uint16_t delta = (uint16_t)(now - d->tick_last);
    d->tick_last = now;
    while (delta >= TICKS_PER_S) {
        delta = (uint16_t)(delta - TICKS_PER_S);
        if (++d->sec_acc >= 60) { d->sec_acc = 0; d->age_m++; }
    }
    d->tick_acc = (uint8_t)(d->tick_acc + delta);
    if (d->tick_acc >= TICKS_PER_S) {
        d->tick_acc = (uint8_t)(d->tick_acc - TICKS_PER_S);
        if (++d->sec_acc >= 60) { d->sec_acc = 0; d->age_m++; }
    }
}

/* The lease ran out, or the server said no: drop the address and start
 * over, as if never configured. */
static void lost(mn_dhcp *d, mn_net *net, uint16_t now)
{
    static const uint8_t zero[4] = { 0, 0, 0, 0 };
    mn_net_set_ip(net, zero, zero, zero);
    d->state = MN_DHCP_SELECTING;
    d->phase = MN_DHCP_PHASE_BOUND;
    d->tries = 0;
    d->xid[3]++;
    if (d->sock == 0xff)
        d->sock = mn_net_udp_open(net, PORT_CLIENT);
    transmit(d, net, MSG_DISCOVER, now);
}

static uint8_t poll_bound(mn_dhcp *d, mn_net *net, uint16_t now)
{
    uint16_t len, sport;
    uint8_t src[4], yiaddr[4], type;

    clock_tick(d, now);
    if (d->t1_m == 0)
        return d->state;                          /* a lease without expiry */

    if (d->sock != 0xff &&
        mn_net_udp_recv(net, d->sock, msg, MAX_MSG, &len, src, &sport)) {
        d->rx_count++;
        d->last_len = len;
        type = parse(d, net, msg, len, yiaddr);
        d->last_type = type;
        if (type == MSG_ACK) {                    /* renewed: new lease, same address */
            mn_net_udp_close(net, d->sock);
            d->sock = 0xff;
            granted(d, now);
            return d->state;
        }
        if (type == MSG_NAK) {
            lost(d, net, now);
            return d->state;
        }
    }

    if (d->age_m >= d->lease_m) {
        lost(d, net, now);
        return d->state;
    }
    if (d->age_m >= d->next_m) {
        d->phase = d->age_m >= d->t2_m ? MN_DHCP_PHASE_REBINDING : MN_DHCP_PHASE_RENEWING;
        if (d->sock == 0xff)
            d->sock = mn_net_udp_open(net, PORT_CLIENT);
        d->tries = 0;
        transmit(d, net, MSG_REQUEST, now);
        d->next_m = (uint16_t)(d->age_m + RENEW_RETRY_M);
        if (d->phase == MN_DHCP_PHASE_RENEWING && d->next_m > d->t2_m)
            d->next_m = d->t2_m;                  /* rebind on time, not late */
    } else if (d->pending) {
        transmit(d, net, MSG_REQUEST, now);       /* the next hop was being resolved */
    }
    return d->state;
}

uint8_t mn_dhcp_poll(mn_dhcp *d, mn_net *net, uint16_t now)
{
    uint16_t len, sport;
    uint8_t src[4], yiaddr[4], type;

    if (d->state == MN_DHCP_IDLE || d->state == MN_DHCP_FAILED)
        return d->state;
    if (d->state == MN_DHCP_BOUND)
        return poll_bound(d, net, now);

    /* Anything for us? */
    if (mn_net_udp_recv(net, d->sock, msg, MAX_MSG, &len, src, &sport)) {
        uint8_t k;
        d->rx_count++;
        d->last_len = len;
        d->last_op = msg[0];
        for (k = 0; k < 4; k++) d->last_xid[k] = msg[4 + k];
        for (k = 0; k < 6; k++) d->last_chaddr[k] = msg[28 + k];
        type = parse(d, net, msg, len, yiaddr);
        d->last_type = type;
        if (d->state == MN_DHCP_SELECTING && type == MSG_OFFER) {
            copy4(d->offered_ip, yiaddr);
            d->state = MN_DHCP_REQUESTING;
            d->tries = 0;
            transmit(d, net, MSG_REQUEST, now);
            return d->state;
        }
        if (d->state == MN_DHCP_REQUESTING) {
            if (type == MSG_ACK) {
                mn_net_set_ip(net, yiaddr, d->mask, d->gw);
                mn_net_udp_close(net, d->sock);
                d->sock = 0xff;
                d->state = MN_DHCP_BOUND;
                granted(d, now);
                return d->state;
            }
            if (type == MSG_NAK) {                /* start over */
                d->state = MN_DHCP_SELECTING;
                d->tries = 0;
                transmit(d, net, MSG_DISCOVER, now);
                return d->state;
            }
        }
    }

    /* Anything due? A pending send is retried at once; a silent server
     * gets another try after RETRY_TICKS, up to MAX_TRIES. */
    if (d->pending) {
        transmit(d, net, d->state == MN_DHCP_SELECTING ? MSG_DISCOVER : MSG_REQUEST, now);
    } else if ((uint16_t)(now - d->sent_at) >= RETRY_TICKS) {
        if (d->tries >= MAX_TRIES) {
            d->state = MN_DHCP_FAILED;
            mn_net_udp_close(net, d->sock);
            d->sock = 0xff;
        } else {
            transmit(d, net, d->state == MN_DHCP_SELECTING ? MSG_DISCOVER : MSG_REQUEST, now);
        }
    }
    return d->state;
}

uint16_t mn_dhcp_lease_left(const mn_dhcp *d)
{
    if (d->state != MN_DHCP_BOUND)
        return 0;
    if (d->t1_m == 0)
        return 0xFFFF;
    return d->age_m < d->lease_m ? (uint16_t)(d->lease_m - d->age_m) : 0;
}

const uint8_t *mn_dhcp_msg_buffer(void)
{
    return msg;
}
