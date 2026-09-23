#include "mn_dns.h"
#include "mn_byteorder.h"

#define PORT_DNS     53
#define HDR_LEN      12
#define TYPE_A       1
#define CLASS_IN     1
#define FLAG_RD      0x0100
#define FLAG_QR      0x8000
#define RCODE_MASK   0x000f

#define RETRY_TICKS  100          /* 2 s at 50 Hz */
#define MAX_TRIES    3
#define MAX_MSG      MN_UDP_SOCK_BUF

/* Built and parsed in the network layer's receive buffer, idle between
 * polls; a message never outlives the poll that handles it (5.12). */
#define msg (mn_net_scratch())

/* Writes the query for d->name. Returns its length, or 0 if the name is
 * malformed (empty label, label over 63). */
static uint16_t build(mn_dns *d)
{
    uint16_t o = HDR_LEN, lenpos;
    uint8_t i, n;

    mn_put16(msg + 0, d->id);
    mn_put16(msg + 2, FLAG_RD);
    mn_put16(msg + 4, 1);                 /* one question */
    mn_put16(msg + 6, 0);
    mn_put16(msg + 8, 0);
    mn_put16(msg + 10, 0);

    lenpos = o++;
    n = 0;
    for (i = 0; d->name[i]; i++) {
        if (d->name[i] == '.') {
            if (n == 0)
                return 0;
            msg[lenpos] = n;
            lenpos = o++;
            n = 0;
        } else {
            msg[o++] = (uint8_t)d->name[i];
            if (++n > 63)
                return 0;
        }
    }
    if (n == 0)
        return 0;
    msg[lenpos] = n;
    msg[o++] = 0;                          /* root */
    mn_put16(msg + o, TYPE_A);  o += 2;
    mn_put16(msg + o, CLASS_IN); o += 2;
    return o;
}

static void transmit(mn_dns *d, mn_net *net, uint16_t now)
{
    uint16_t n = build(d);
    uint8_t r;
    if (n == 0) {
        d->state = MN_DNS_FAILED;
        return;
    }
    r = mn_net_send_udp(net, d->server, d->port, PORT_DNS, msg, n);
    d->pending = (uint8_t)(r == MN_SEND_PENDING);
    d->sent_at = now;
    if (r != MN_SEND_PENDING)
        d->tries++;
}

void mn_dns_start(mn_dns *d, mn_net *net, const char *name,
                  const uint8_t *server, uint16_t now)
{
    uint8_t i;

    for (i = 0; i < MN_DNS_NAME_MAX && name[i]; i++)
        d->name[i] = name[i];
    d->name[i] = 0;
    for (i = 0; i < 4; i++) {
        d->server[i] = server[i];
        d->ip[i] = 0;
    }
    if (d->sock != 0xff)
        mn_net_udp_close(net, d->sock);
    /* An id and port that differ between lookups. */
    d->id = (uint16_t)(now * 7 + net->nif->mac[5]);
    d->port = (uint16_t)(49152 + (now & 0x0fff));
    d->sock = mn_net_udp_open(net, d->port);
    d->tries = 0;
    d->state = d->sock == 0xff ? MN_DNS_FAILED : MN_DNS_WAITING;
    if (d->state == MN_DNS_WAITING)
        transmit(d, net, now);
}

/* Skips a name at m[o] (labels or a compression pointer). Returns the
 * offset after it, or 0 if it runs off the end. */
static uint16_t skip_name(const uint8_t *m, uint16_t o, uint16_t len)
{
    uint8_t n;
    while (o < len) {
        n = m[o];
        if (n == 0)
            return (uint16_t)(o + 1);
        if ((n & 0xc0) == 0xc0)
            return (uint16_t)(o + 2 <= len ? o + 2 : 0);
        o = (uint16_t)(o + 1 + n);
    }
    return 0;
}

/* Returns 1 with d->ip filled if m is a valid answer to our query. */
static uint8_t parse(mn_dns *d, const uint8_t *m, uint16_t len)
{
    uint16_t flags, qd, an, o, type, cls, rdlen;
    uint8_t i;

    if (len < HDR_LEN || mn_get16(m) != d->id)
        return 0;
    flags = mn_get16(m + 2);
    if (!(flags & FLAG_QR))
        return 0;
    if (flags & RCODE_MASK) {              /* NXDOMAIN, SERVFAIL, ... */
        d->state = MN_DNS_FAILED;
        return 0;
    }
    qd = mn_get16(m + 4);
    an = mn_get16(m + 6);
    o = HDR_LEN;
    while (qd--) {                         /* skip the question(s) */
        o = skip_name(m, o, len);
        if (o == 0 || (uint16_t)(o + 4) > len)
            return 0;
        o = (uint16_t)(o + 4);
    }
    while (an--) {
        o = skip_name(m, o, len);
        if (o == 0 || (uint16_t)(o + 10) > len)
            return 0;
        type = mn_get16(m + o);
        cls = mn_get16(m + o + 2);
        rdlen = mn_get16(m + o + 8);
        o = (uint16_t)(o + 10);
        if ((uint16_t)(o + rdlen) > len)
            return 0;
        if (type == TYPE_A && cls == CLASS_IN && rdlen == 4) {
            for (i = 0; i < 4; i++)
                d->ip[i] = m[o + i];
            return 1;
        }
        o = (uint16_t)(o + rdlen);         /* a CNAME, say: keep going */
    }
    return 0;
}

uint8_t mn_dns_poll(mn_dns *d, mn_net *net, uint16_t now)
{
    uint16_t len, sport;
    uint8_t src[4];

    if (d->state != MN_DNS_WAITING)
        return d->state;

    if (mn_net_udp_recv(net, d->sock, msg, MAX_MSG, &len, src, &sport)) {
        if (parse(d, msg, len)) {
            d->state = MN_DNS_DONE;
        }
        if (d->state != MN_DNS_WAITING) {
            mn_net_udp_close(net, d->sock);
            d->sock = 0xff;
            return d->state;
        }
    }

    if (d->pending) {
        transmit(d, net, now);
    } else if ((uint16_t)(now - d->sent_at) >= RETRY_TICKS) {
        if (d->tries >= MAX_TRIES) {
            d->state = MN_DNS_FAILED;
            mn_net_udp_close(net, d->sock);
            d->sock = 0xff;
        } else {
            transmit(d, net, now);
        }
    }
    return d->state;
}
