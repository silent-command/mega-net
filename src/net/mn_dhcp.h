/* DHCP client (RFC 2131/2132), as a state machine driven by poll. Time
 * is whatever tick the caller passes -- on the MEGA65, the frame counter
 * -- so this file knows nothing about hardware. */
#ifndef MN_DHCP_H
#define MN_DHCP_H

#include <stdint.h>
#include "mn_net.h"

#define MN_DHCP_IDLE       0
#define MN_DHCP_SELECTING  1   /* DISCOVER sent, waiting for an OFFER */
#define MN_DHCP_REQUESTING 2   /* REQUEST sent, waiting for an ACK */
#define MN_DHCP_BOUND      3   /* the address is valid; renewal runs underneath */
#define MN_DHCP_FAILED     4

/* While BOUND, the phase: the lease is renewed at T1 (half the lease) by
 * a unicast REQUEST to the server, rebound at T2 (seven eighths) by a
 * broadcast one, and when it expires the address is dropped and
 * acquisition starts over (RFC 2131 4.4.5). The application keeps
 * seeing BOUND until then. */
#define MN_DHCP_PHASE_BOUND     0
#define MN_DHCP_PHASE_RENEWING  1
#define MN_DHCP_PHASE_REBINDING 2

typedef struct {
    uint8_t  state;
    uint8_t  sock;
    uint8_t  xid[4];
    uint8_t  offered_ip[4];
    uint8_t  server_id[4];
    uint8_t  mask[4];
    uint8_t  gw[4];
    uint8_t  dns[4];
    uint32_t lease_s;
    uint8_t  phase;            /* MN_DHCP_PHASE_* while BOUND */
    /* The lease clock runs in minutes, 16 bits: a lease of 45 days or more
     * counts as one without expiry, and the arithmetic stays cheap on a
     * 6502. The tick feeds a seconds counter, the seconds a minute. */
    uint16_t age_m;            /* minutes since the lease was granted */
    uint16_t t1_m, t2_m;       /* renew and rebind at these ages; 0 = never */
    uint16_t next_m;           /* age at which the next renewal REQUEST goes out */
    uint16_t lease_m;
    uint16_t tick_last;
    uint8_t  tick_acc, sec_acc;
    uint16_t sent_at;          /* tick of the last transmission */
    uint8_t  tries;            /* transmissions in the current state */
    uint8_t  pending;          /* a send returned PENDING; retry soon */
    /* Diagnostics: the last datagram the socket delivered. */
    uint16_t rx_count;
    uint16_t last_len;
    uint8_t  last_op, last_type;
    uint8_t  last_xid[4];
    uint8_t  last_chaddr[6];
} mn_dhcp;

/* Starts (or restarts) acquisition. Opens the socket. */
void mn_dhcp_start(mn_dhcp *d, mn_net *net, uint16_t now);

/* Advances the machine: sends what is due, consumes what has arrived.
 * Returns the state. Call it from the network poll. */
uint8_t mn_dhcp_poll(mn_dhcp *d, mn_net *net, uint16_t now);

/* Minutes of lease left; 65535 for a lease without expiry; 0 when not
 * bound. */
uint16_t mn_dhcp_lease_left(const mn_dhcp *d);

/* Diagnostics: the buffer holding the last message sent or received. */
const uint8_t *mn_dhcp_msg_buffer(void);

#endif
