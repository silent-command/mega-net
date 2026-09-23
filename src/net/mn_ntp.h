/* SNTP client (RFC 4330): one request, one reply, the time. Plus the
 * arithmetic to turn seconds-since-1900 into a calendar date. */
#ifndef MN_NTP_H
#define MN_NTP_H

#include <stdint.h>
#include "mn_net.h"

#define MN_NTP_IDLE    0
#define MN_NTP_WAITING 1
#define MN_NTP_DONE    2
#define MN_NTP_FAILED  3

typedef struct {
    uint8_t  state;
    uint8_t  sock;
    uint16_t port;
    uint8_t  server[4];
    uint32_t seconds;          /* since 1900-01-01 00:00:00 UTC, when DONE */
    uint16_t sent_at;
    uint8_t  tries;
    uint8_t  pending;
} mn_ntp;

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, weekday;   /* weekday 0 = Sunday */
} mn_civil;

void mn_ntp_start(mn_ntp *n, mn_net *net, const uint8_t *server, uint16_t now);
uint8_t mn_ntp_poll(mn_ntp *n, mn_net *net, uint16_t now);

/* seconds since 1900 plus an offset in minutes (local time) to a date. */
void mn_ntp_civil(uint32_t seconds_1900, int16_t offset_min, mn_civil *out);

#endif
