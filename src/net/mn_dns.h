/* DNS resolver: one A-record lookup at a time, driven by poll. */
#ifndef MN_DNS_H
#define MN_DNS_H

#include <stdint.h>
#include "mn_net.h"

#define MN_DNS_IDLE    0
#define MN_DNS_WAITING 1
#define MN_DNS_DONE    2
#define MN_DNS_FAILED  3

#define MN_DNS_NAME_MAX 63

typedef struct {
    uint8_t  state;
    uint8_t  sock;
    uint16_t id;
    uint16_t port;
    uint8_t  server[4];
    uint8_t  ip[4];            /* the answer, when DONE */
    char     name[MN_DNS_NAME_MAX + 1];
    uint16_t sent_at;
    uint8_t  tries;
    uint8_t  pending;
} mn_dns;

/* Begins a lookup of name (a dotted hostname, NUL-terminated, at most
 * MN_DNS_NAME_MAX chars) at server. */
void mn_dns_start(mn_dns *d, mn_net *net, const char *name,
                  const uint8_t *server, uint16_t now);

/* Advances the lookup. Returns the state. */
uint8_t mn_dns_poll(mn_dns *d, mn_net *net, uint16_t now);

#endif
