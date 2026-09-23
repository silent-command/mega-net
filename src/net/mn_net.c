#include "mn_net.h"
#include "mn_xmem.h"
#include "mn_ether.h"
#include "mn_arp.h"
#include "mn_ipv4.h"
#include "mn_icmp.h"
#include "mn_udp.h"

/* Two frames: what came in, and what goes out. Never overlaid. */
static uint8_t rxf[MN_MAX_FRAME];
static uint8_t txf[MN_MAX_FRAME];

uint8_t *mn_net_scratch(void) { return rxf; }

static const uint8_t limited_bcast[4] = { 255, 255, 255, 255 };
static const uint8_t zero_ip[4] = { 0, 0, 0, 0 };

static void copy4(uint8_t *d, const uint8_t *s)
{
    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
}

static uint8_t eq4(const uint8_t *a, const uint8_t *b)
{
    return (uint8_t)(a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]);
}

void mn_net_init(mn_net *net, mn_netif *nif)
{
    uint8_t i;
    net->nif = nif;
    for (i = 0; i < 4; i++)
        net->ip[i] = net->mask[i] = net->gw[i] = 0;
    mn_arp_cache_init(&net->arp);
    for (i = 0; i < MN_UDP_SOCKETS; i++) {
        net->sock[i].port = 0;
        net->sock[i].pending = 0;
    }
    mn_net_set_pool(net, MN_XMEM_BASE + 0xE000UL);   /* the host default: after eight 7 KB TCP sockets */
    net->ip_id = 1;
    net->rx_frames = net->arp_replies = net->arp_learned = 0;
    net->echo_replies = net->udp_rx = net->udp_dropped = 0;
    net->tx_failures = 0;
    net->tcp_input = 0;
    net->tcp_ctx = 0;
}

void mn_net_set_ip(mn_net *net, const uint8_t *ip, const uint8_t *mask,
                   const uint8_t *gw)
{
    copy4(net->ip, ip);
    copy4(net->mask, mask);
    copy4(net->gw, gw);
}

static uint8_t send(mn_net *net, uint16_t len)
{
    if (net->nif->tx(net->nif, txf, len))
        return MN_SEND_OK;
    net->tx_failures++;
    return MN_SEND_FAILED;
}

/* --- receive side --- */

static void deliver_udp(mn_net *net, const uint8_t *ip, uint16_t iplen)
{
    const uint8_t *udp = mn_ipv4_payload(ip);
    uint16_t ulen = mn_ipv4_payload_len(ip);
    uint16_t port;
    uint8_t i;
    mn_udp_sock *s;
    (void)iplen;

    if (!mn_udp_valid(mn_ipv4_src(ip), mn_ipv4_dst(ip), udp, ulen))
        return;
    port = mn_udp_dport(udp);
    for (i = 0; i < MN_UDP_SOCKETS; i++) {
        s = &net->sock[i];
        if (s->port == port) {
            uint16_t plen = (uint16_t)(ulen - MN_UDP_HDR_LEN);
            if (s->pending || plen > MN_UDP_SOCK_BUF) {
                net->udp_dropped++;
                return;
            }
            mn_xmem_write(s->xseg, s->xoff, udp + MN_UDP_HDR_LEN, plen);
            s->len = plen;
            copy4(s->src_ip, mn_ipv4_src(ip));
            s->src_port = mn_udp_sport(udp);
            s->pending = 1;
            net->udp_rx++;
            return;
        }
    }
    net->udp_dropped++;
}

void mn_net_set_pool(mn_net *net, uint32_t base)
{
    uint8_t i;
    for (i = 0; i < MN_UDP_SOCKETS; i++) {
        net->sock[i].xseg = (uint16_t)(base >> 16);
        net->sock[i].xoff = (uint16_t)(base + (uint32_t)i * MN_UDP_SOCK_BUF);
    }
}

uint8_t mn_net_poll(mn_net *net)
{
    uint16_t len, n;
    const uint8_t *ip;

    if (!net->nif->rx(net->nif, rxf, sizeof rxf, &len))
        return 0;
    net->rx_frames++;

    if (len < MN_ETH_HDR_LEN || !mn_ether_is_for_us(rxf, net->nif->mac))
        return 1;

    switch (mn_ether_type(rxf)) {
    case MN_ETHERTYPE_ARP:
        n = mn_arp_opcode(rxf, len);
        if (n == MN_ARP_OP_REPLY) {
            mn_arp_cache_learn(&net->arp, mn_arp_sender_ip(rxf),
                               mn_arp_sender_mac(rxf));
            net->arp_learned++;
        } else if (mn_arp_wants_us(rxf, len, net->ip)) {
            /* Whoever asks for us is worth remembering too. */
            mn_arp_cache_learn(&net->arp, mn_arp_sender_ip(rxf),
                               mn_arp_sender_mac(rxf));
            n = mn_arp_build_reply(txf, rxf, net->nif->mac, net->ip);
            send(net, n);
            net->arp_replies++;
        }
        break;
    case MN_ETHERTYPE_IPV4:
        ip = rxf + MN_ETH_HDR_LEN;
        if (len < MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN ||
            !mn_ipv4_valid(ip, (uint16_t)(len - MN_ETH_HDR_LEN)))
            break;
        /* Ours, broadcast, or -- while we have no address yet -- anything
         * that reached us by MAC: a DHCP server that ignores the broadcast
         * flag unicasts the offer to the address it is about to give us. */
        if (!mn_ipv4_addr_eq(mn_ipv4_dst(ip), net->ip) &&
            !mn_ipv4_addr_eq(mn_ipv4_dst(ip), limited_bcast) &&
            !mn_ipv4_addr_eq(net->ip, zero_ip))
            break;
        if (mn_ipv4_proto(ip) == MN_IPPROTO_ICMP) {
            if (mn_icmp_is_echo_request(rxf, len, net->ip)) {
                n = mn_icmp_build_echo_reply(txf, rxf, len, net->nif->mac, net->ip);
                send(net, n);
                net->echo_replies++;
            }
        } else if (mn_ipv4_proto(ip) == MN_IPPROTO_UDP) {
            deliver_udp(net, ip, (uint16_t)(len - MN_ETH_HDR_LEN));
        } else if (mn_ipv4_proto(ip) == MN_IPPROTO_TCP && net->tcp_input) {
            net->tcp_input(net->tcp_ctx, net, ip, (uint16_t)(len - MN_ETH_HDR_LEN));
        }
        break;
    default:
        break;
    }
    return 1;
}

/* --- transmit side --- */

static uint8_t is_broadcast(const mn_net *net, const uint8_t *dst)
{
    uint8_t i;
    if (eq4(dst, limited_bcast))
        return 1;
    for (i = 0; i < 4; i++)                       /* subnet broadcast */
        if ((uint8_t)(dst[i] | net->mask[i]) != 0xff)
            return 0;
    return 1;
}

const uint8_t *mn_net_next_hop(const mn_net *net, const uint8_t *dst)
{
    uint8_t i;
    if (is_broadcast(net, dst))
        return dst;
    for (i = 0; i < 4; i++)
        if ((dst[i] & net->mask[i]) != (net->ip[i] & net->mask[i]))
            return net->gw;
    return dst;
}

static const uint8_t *tx_mac;      /* resolved by tx_begin for tx_send */

uint8_t *mn_net_tx_begin(mn_net *net, const uint8_t *dst_ip, uint8_t *why)
{
    const uint8_t *hop = mn_net_next_hop(net, dst_ip);

    if (is_broadcast(net, hop)) {
        tx_mac = mn_eth_broadcast;
    } else {
        tx_mac = mn_arp_cache_lookup(&net->arp, hop);
        if (!tx_mac) {
            uint16_t n = mn_arp_build_request(txf, net->nif->mac, net->ip, hop);
            send(net, n);
            *why = MN_SEND_PENDING;
            return 0;
        }
    }
    *why = MN_SEND_OK;
    return txf + MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN;
}

uint8_t mn_net_tx_send(mn_net *net, const uint8_t *dst_ip, uint8_t proto,
                       uint16_t payload_len)
{
    uint8_t *ip = txf + MN_ETH_HDR_LEN;
    mn_ipv4_build(ip, net->ip, dst_ip, proto, payload_len, net->ip_id++);
    mn_ether_build(txf, tx_mac, net->nif->mac, MN_ETHERTYPE_IPV4);
    return send(net, (uint16_t)(MN_ETH_HDR_LEN + MN_IPV4_HDR_LEN + payload_len));
}

uint8_t mn_net_send_udp(mn_net *net, const uint8_t *dst_ip, uint16_t sport,
                        uint16_t dport, const uint8_t *payload, uint16_t len)
{
    uint8_t *udp, why;
    uint16_t i, ulen;

    if (len > MN_UDP_MAX_PAYLOAD)
        return MN_SEND_FAILED;
    udp = mn_net_tx_begin(net, dst_ip, &why);
    if (!udp)
        return why;
    for (i = 0; i < len; i++)
        udp[MN_UDP_HDR_LEN + i] = payload[i];
    ulen = mn_udp_build(udp, net->ip, dst_ip, sport, dport, len);
    return mn_net_tx_send(net, dst_ip, MN_IPPROTO_UDP, ulen);
}

/* --- sockets --- */

uint8_t mn_net_udp_open(mn_net *net, uint16_t port)
{
    uint8_t i;
    for (i = 0; i < MN_UDP_SOCKETS; i++)
        if (net->sock[i].port == port)
            return 0xff;                          /* already open */
    for (i = 0; i < MN_UDP_SOCKETS; i++) {
        if (net->sock[i].port == 0) {
            net->sock[i].port = port;
            net->sock[i].pending = 0;
            return i;
        }
    }
    return 0xff;
}

void mn_net_udp_close(mn_net *net, uint8_t s)
{
    if (s < MN_UDP_SOCKETS) {
        net->sock[s].port = 0;
        net->sock[s].pending = 0;
    }
}

uint8_t mn_net_udp_recv(mn_net *net, uint8_t s, uint8_t *buf, uint16_t cap,
                        uint16_t *len, uint8_t *src_ip, uint16_t *src_port)
{
    mn_udp_sock *k;
    uint16_t n;

    if (s >= MN_UDP_SOCKETS)
        return 0;
    k = &net->sock[s];
    if (!k->pending)
        return 0;
    n = k->len;
    if (n > cap)
        n = cap;
    mn_xmem_read(buf, k->xseg, k->xoff, n);
    *len = n;
    if (src_ip)
        copy4(src_ip, k->src_ip);
    if (src_port)
        *src_port = k->src_port;
    k->pending = 0;
    return 1;
}
