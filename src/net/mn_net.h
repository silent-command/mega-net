/* Copyright 2026 Justin Cameron
 * SPDX-License-Identifier: Apache-2.0 */
/* The network layer: one interface, one IPv4 address, a small ARP cache,
 * a few UDP sockets, and a poll that pulls a frame and dispatches it.
 * Everything above the link is driven from here. Nothing blocks. */
#ifndef MN_NET_H
#define MN_NET_H

#include <stdint.h>
#include "mn_netif.h"
#include "mn_arp.h"

#define MN_UDP_SOCKETS   4   /* DHCP, DNS and NTP each borrow one while they run */
#define MN_UDP_SOCK_BUF  576      /* a DHCP message fits */
#define MN_UDP_MAX_PAYLOAD (MN_MAX_FRAME - 14 - 20 - 8)

/* Results of a send. */
#define MN_SEND_FAILED   0
#define MN_SEND_OK       1
#define MN_SEND_PENDING  2        /* ARP sent for the next hop; try again */
#define MN_SEND_NOROUTE  3        /* the next hop never answered ARP */

/* Resolving a next hop: one lookup at a time, a request every
 * MN_ARP_RETRY ticks, given up after MN_ARP_TRIES (RFC 1122 2.3.2.1),
 * and the failure remembered for MN_ARP_HOLD ticks so that whoever is
 * waiting on that hop hears of it. */
#define MN_ARP_RETRY 25           /* 0.5 s at 50 Hz */
#define MN_ARP_TRIES 5
#define MN_ARP_HOLD  50
#define MN_ARP_IDLE   0
#define MN_ARP_ASKING 1
#define MN_ARP_FAILED 2

typedef struct {
    uint16_t port;                /* 0 = closed */
    uint8_t  pending;             /* a datagram is waiting */
    uint8_t  src_ip[4];
    uint16_t src_port;
    uint16_t len;
    uint16_t xseg, xoff;          /* the datagram itself, in the pool (mn_xmem.h) */
} mn_udp_sock;

struct mn_net;
typedef void (*mn_ip_input_fn)(void *ctx, struct mn_net *net,
                               const uint8_t *ip, uint16_t len);

typedef struct mn_net {
    mn_netif *nif;
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gw[4];
    mn_arp_cache arp;
    mn_udp_sock sock[MN_UDP_SOCKETS];
    uint16_t ip_id;
    uint16_t rx_frames;
    uint16_t arp_replies;
    uint16_t arp_learned;
    uint16_t echo_replies;
    uint16_t udp_rx;
    uint16_t udp_dropped;         /* no socket, or its mailbox was full */
    uint16_t tx_failures;
    /* Whoever wants IPv4 packets of another protocol (TCP). */
    mn_ip_input_fn tcp_input;
    void *tcp_ctx;
    /* The lookup in progress (MN_ARP_*), and the tick it is timed by. */
    uint8_t  arp_ip[4];
    uint8_t  arp_state, arp_tries;
    uint16_t arp_at;
    uint16_t now;
} mn_net;

void mn_net_init(mn_net *net, mn_netif *nif);

/* The tick the ARP timers run on. Every poller that has one passes it
 * in (TCP, DHCP, DNS, NTP), so this layer needs no clock of its own. */
#define mn_net_tick(net, t) ((net)->now = (t))

/* Where the UDP mailboxes live: MN_UDP_SOCKETS * MN_UDP_SOCK_BUF bytes of
 * pool from base, which must not cross a 64 KB boundary. mn_net_init sets
 * a default that suits the host tests; the image sets its own (5.17). */
void mn_net_set_pool(mn_net *net, uint32_t base);

/* The receive frame buffer, which is only in use inside mn_net_poll: a
 * caller may borrow it as MN_MAX_FRAME bytes of scratch between polls.
 * The ABI stages raw frames and UDP payloads through it (5.11). */
uint8_t *mn_net_scratch(void);
void mn_net_set_ip(mn_net *net, const uint8_t *ip, const uint8_t *mask,
                   const uint8_t *gw);

/* Pulls at most one frame from the link and handles it. Returns 1 if a
 * frame was pulled. */
uint8_t mn_net_poll(mn_net *net);

/* The address a packet for dst is actually sent to: dst itself when it
 * is on our subnet or a broadcast, else the gateway. */
const uint8_t *mn_net_next_hop(const mn_net *net, const uint8_t *dst);

/* Sends payload as UDP. MN_SEND_PENDING means the next hop's MAC is not
 * yet known: nothing was sent, call again (a request goes out at most
 * every MN_ARP_RETRY ticks). MN_SEND_NOROUTE means the next hop did
 * not answer MN_ARP_TRIES requests. */
uint8_t mn_net_send_udp(mn_net *net, const uint8_t *dst_ip, uint16_t sport,
                        uint16_t dport, const uint8_t *payload, uint16_t len);

/* Raw IPv4 transmit in two steps, so a transport can build its segment
 * in place. mn_net_tx_begin resolves the next hop and returns where the
 * IPv4 payload goes (NULL with *why = MN_SEND_PENDING or MN_SEND_NOROUTE);
 * mn_net_tx_send fills the headers and transmits. */
uint8_t *mn_net_tx_begin(mn_net *net, const uint8_t *dst_ip, uint8_t *why);
uint8_t  mn_net_tx_send(mn_net *net, const uint8_t *dst_ip, uint8_t proto,
                        uint16_t payload_len);

/* Sockets: a port with a one-datagram mailbox. Open returns an index or
 * 0xFF. Receive copies the waiting datagram out and frees the mailbox;
 * returns 1 if there was one. */
uint8_t mn_net_udp_open(mn_net *net, uint16_t port);
void    mn_net_udp_close(mn_net *net, uint8_t s);
uint8_t mn_net_udp_recv(mn_net *net, uint8_t s, uint8_t *buf, uint16_t cap,
                        uint16_t *len, uint8_t *src_ip, uint16_t *src_port);

#endif
