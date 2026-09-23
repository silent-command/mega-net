#include "mn_ntp.h"
#include "mn_byteorder.h"

#define PORT_NTP    123
#define MSG_LEN     48
#define RETRY_TICKS 150           /* 3 s */
#define MAX_TRIES   3

static uint8_t msg[MSG_LEN] MN_BSS("msg");

static void transmit(mn_ntp *n, mn_net *net, uint16_t now)
{
    uint8_t i, r;
    for (i = 0; i < MSG_LEN; i++)
        msg[i] = 0;
    msg[0] = 0x23;                     /* LI 0, version 4, mode 3 (client) */
    r = mn_net_send_udp(net, n->server, n->port, PORT_NTP, msg, MSG_LEN);
    n->pending = (uint8_t)(r == MN_SEND_PENDING);
    n->sent_at = now;
    if (r != MN_SEND_PENDING)
        n->tries++;
}

void mn_ntp_start(mn_ntp *n, mn_net *net, const uint8_t *server, uint16_t now)
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        n->server[i] = server[i];
    n->seconds = 0;
    if (n->sock != 0xff)
        mn_net_udp_close(net, n->sock);
    n->port = (uint16_t)(50000 + (now & 0x0fff));
    n->sock = mn_net_udp_open(net, n->port);
    n->tries = 0;
    n->state = n->sock == 0xff ? MN_NTP_FAILED : MN_NTP_WAITING;
    if (n->state == MN_NTP_WAITING)
        transmit(n, net, now);
}

uint8_t mn_ntp_poll(mn_ntp *n, mn_net *net, uint16_t now)
{
    uint16_t len, sport;
    uint8_t src[4], mode;

    if (n->state != MN_NTP_WAITING)
        return n->state;

    if (mn_net_udp_recv(net, n->sock, msg, sizeof msg, &len, src, &sport)) {
        mode = msg[0] & 0x07;
        /* Server reply: mode 4 (or 2, broadcast/symmetric), and a
         * transmit timestamp. Anything else is ignored. */
        if (len >= MSG_LEN && (mode == 4 || mode == 2) &&
            mn_get32(msg + 40) != 0) {
            n->seconds = mn_get32(msg + 40);
            n->state = MN_NTP_DONE;
            mn_net_udp_close(net, n->sock);
            n->sock = 0xff;
            return n->state;
        }
    }

    if (n->pending) {
        transmit(n, net, now);
    } else if ((uint16_t)(now - n->sent_at) >= RETRY_TICKS) {
        if (n->tries >= MAX_TRIES) {
            n->state = MN_NTP_FAILED;
            mn_net_udp_close(net, n->sock);
            n->sock = 0xff;
        } else {
            transmit(n, net, now);
        }
    }
    return n->state;
}

static uint8_t leap(uint16_t y)
{
    return (uint8_t)((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
}

void mn_ntp_civil(uint32_t seconds_1900, int16_t offset_min, mn_civil *out)
{
    static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t s = (uint32_t)((int32_t)seconds_1900 + (int32_t)offset_min * 60);
    uint32_t days, rem;

    /* NTP's 32-bit seconds wrap on 2036-02-07. A value below the pivot is
     * taken as the next era (2036-2104), the way RFC 4330 suggests: add
     * 2^32 seconds, which is 49710 days and 23296 seconds. Nobody is
     * setting a MEGA65's clock to 1967. */
    if (s < 0x80000000UL) {
        days = 49710UL + (s + 23296UL) / 86400UL;
        rem = (s + 23296UL) % 86400UL;
    } else {
        days = s / 86400UL;
        rem = s % 86400UL;
    }
    uint16_t y = 1900;
    uint16_t dy;
    uint8_t m;

    out->hour = (uint8_t)(rem / 3600UL);
    rem %= 3600UL;
    out->minute = (uint8_t)(rem / 60UL);
    out->second = (uint8_t)(rem % 60UL);
    out->weekday = (uint8_t)((days + 1) % 7);       /* 1900-01-01 was a Monday */

    for (;;) {
        dy = leap(y) ? 366 : 365;
        if (days < dy)
            break;
        days -= dy;
        y++;
    }
    out->year = y;
    for (m = 0; m < 12; m++) {
        dy = mdays[m];
        if (m == 1 && leap(y))
            dy = 29;
        if (days < dy)
            break;
        days -= dy;
    }
    out->month = (uint8_t)(m + 1);
    out->day = (uint8_t)(days + 1);
}
