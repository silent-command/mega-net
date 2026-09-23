/* TCP: a set of sockets, each an active or passive connection. A receive
 * ring the application drains at its own pace, a stop-and-wait sender,
 * and a state machine driven from poll. Designed around three lessons
 * from the gopher project: the ring stays readable after the peer
 * closes; the window is advertised by its right edge, not its size; and
 * a request goes out as one segment. The sender keeps up to a small
 * congestion window of segments in flight (5.17). The rings live in external memory
 * (mn_xmem.h), so a socket costs the image only its state (5.11). */
#ifndef MN_TCP_H
#define MN_TCP_H

#include <stdint.h>
#include "mn_net.h"

#define MN_TCP_CLOSED      0
#define MN_TCP_SYN_SENT    1
#define MN_TCP_ESTABLISHED 2
#define MN_TCP_FIN_WAIT_1  3
#define MN_TCP_FIN_WAIT_2  4
#define MN_TCP_CLOSE_WAIT  5
#define MN_TCP_CLOSING     6
#define MN_TCP_LAST_ACK    7
#define MN_TCP_TIME_WAIT   8
#define MN_TCP_LISTEN      9
#define MN_TCP_SYN_RCVD    10

/* Flags reported alongside the state. */
#define MN_TCP_F_EOF     0x01   /* the peer has finished sending */
#define MN_TCP_F_RESET   0x02   /* the peer reset the connection */
#define MN_TCP_F_TIMEOUT 0x04   /* retransmissions exhausted */
#define MN_TCP_F_REFUSED 0x08   /* SYN answered with RST */

#define MN_TCP_RCVBUF 4096
#define MN_TCP_SNDBUF 3072
#define MN_TCP_SOCKET_XMEM (MN_TCP_RCVBUF + MN_TCP_SNDBUF)  /* pool bytes per socket */
#define MN_TCP_MSS    1460      /* what we accept */
#define MN_TCP_WINDOW_CAP 2048  /* what we advertise, at most: the link's
                                   receive ring is four frames deep */

typedef struct {
    uint8_t  state, flags, index;
    uint8_t  remote_ip[4];
    uint16_t remote_port, local_port;
    uint32_t iss, snd_una, snd_nxt;
    uint32_t irs, rcv_nxt;
    uint32_t adv_edge;          /* right edge of the last advertised window */
    uint16_t snd_wnd, peer_mss;
    uint16_t xseg, rcvbuf, sndbuf; /* rings in external memory: segment, offsets */
    uint16_t snd_head, snd_len; /* send ring: oldest unacknowledged byte, bytes queued */
    uint16_t inflight;          /* bytes of the send ring sent and unacknowledged */
    uint16_t cwnd;              /* bytes we allow ourselves in flight (5.17) */
    uint8_t  fin_queued, fin_sent;
    uint16_t rcv_head, rcv_count;
    uint8_t  ack_pending, syn_pending, tx_pending;
    uint16_t sent_at, rto, timewait_at;
    uint8_t  retries;
    uint16_t stat_rx_seg, stat_tx_seg, stat_retrans, stat_ooo;
} mn_tcp;

/* The sockets, as one set: the input dispatcher and the poll walk it. */
typedef struct {
    mn_tcp *sock;
    uint8_t n;
    uint16_t rst_sent;          /* segments answered with RST: nobody's */
    uint16_t now;               /* the last poll's tick, for passive opens */
} mn_tcp_set;

/* Sets up n sockets with their rings at xmem_base onwards (n *
 * MN_TCP_SOCKET_XMEM bytes, which must not cross a 64 KB boundary: a
 * 64 KB-aligned base is safe for up to twelve sockets) and wires the set
 * into net. */
uint8_t mn_tcp_init(mn_tcp_set *set, mn_tcp *socks, uint8_t n, mn_net *net,
                    uint32_t xmem_base);   /* returns the sockets set up: fewer
                                              than n if the pool would cross
                                              a 64 KB boundary */

/* Begins a connection. Returns 1 if started (state SYN_SENT). */
uint8_t mn_tcp_connect(mn_tcp *t, mn_net *net, const uint8_t *ip,
                       uint16_t port, uint16_t now);

/* Waits for a connection to port. The socket goes ESTABLISHED when a peer
 * completes the handshake; after that connection ends it is CLOSED, and
 * listen must be called again to take another. Several sockets may
 * listen on one port: each SYN takes one of them. Returns 1 if listening. */
uint8_t mn_tcp_listen(mn_tcp *t, uint16_t port);

/* Queues bytes to send. Returns how many were accepted. */
uint16_t mn_tcp_send(mn_tcp *t, const uint8_t *data, uint16_t len);
uint16_t mn_tcp_send_x(mn_tcp *t, uint32_t src, uint16_t len);   /* from 28-bit memory */

/* Takes bytes from the receive ring. Returns how many. Works in every
 * state: what arrived before a close is still there. */
uint16_t mn_tcp_recv(mn_tcp *t, uint8_t *out, uint16_t cap);
uint16_t mn_tcp_recv_x(mn_tcp *t, uint32_t dst, uint16_t cap);   /* to 28-bit memory */

uint16_t mn_tcp_available(const mn_tcp *t);

/* Graceful close: FIN after the queued data. */
void mn_tcp_close(mn_tcp *t);

/* Drop everything now: RST if there is a peer, then CLOSED. */
void mn_tcp_abort(mn_tcp *t, mn_net *net);

/* Timers and pending transmissions for every socket. Call from the
 * network poll. */
void mn_tcp_poll(mn_tcp_set *set, mn_net *net, uint16_t now);

/* Wired into net->tcp_input by mn_tcp_init. */
void mn_tcp_input(void *ctx, mn_net *net, const uint8_t *ip, uint16_t len);

#endif
