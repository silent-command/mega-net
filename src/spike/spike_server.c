/* Step 6 acceptance: the MEGA65 as a server. Two sockets listen on port
 * 6400 and echo what they receive, listening again when a caller hangs
 * up; a UDP socket on 6401 echoes datagrams. A third TCP caller while
 * both are busy must be refused (RST). tools/server_test.py on the Mac
 * drives it. Status at $1400 (see the ST() offsets). Runs two minutes. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
#define PORT 6400
#define UDP_PORT 6401
#define LINES 2
static uint8_t buf[512];

int main(void)
{
    uint8_t i, st, fl, dhcp = 0, where = 0, us = 0xff, got;
    uint16_t avail, n, base12 = 0, len, sport;
    uint8_t last = MN_FRAMECOUNT, ip[4], sip[4];
    uint16_t frames = 0, accepted[LINES] = {0, 0}, echoed[LINES] = {0, 0}, udp_echoed = 0;
    uint8_t live[LINES] = {0, 0};

    for (i = 0; i < 64; i++) ST(i) = 0;
    ST(0) = 'S'; ST(1) = 'V';
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    ST(2) = meganet_tcp_info(&where, &base12);      /* sockets */
    ST(3) = where; ST(4) = (uint8_t)base12; ST(5) = (uint8_t)(base12 >> 8);
    meganet_dhcp_start();

    for (;;) {
        meganet_poll();
        if (MN_FRAMECOUNT != last) { last = MN_FRAMECOUNT; frames++; }
        if (!dhcp) {
            if (meganet_dhcp_state() == MEGANET_DHCP_BOUND) {
                meganet_ipconf_t c = {{0,0,0,0},{0,0,0,0},{0,0,0,0}};
                meganet_call_ptr(MEGANET_GET_IP, &c);
                ip[0] = c.ip[0]; ip[1] = c.ip[1]; ip[2] = c.ip[2]; ip[3] = c.ip[3];
                ST(6) = ip[0]; ST(7) = ip[1]; ST(8) = ip[2]; ST(9) = ip[3];
                for (i = 0; i < LINES; i++) meganet_tcp_listen(i, PORT);
                us = meganet_udp_open(UDP_PORT);
                ST(10) = us;
                dhcp = 1;
            } else if (frames > 1500) break;
            continue;
        }
        for (i = 0; i < LINES; i++) {
            st = meganet_tcp_state_s(i, &fl, &avail);
            ST(16 + i) = st;
            if (st == MEGANET_TCP_ESTABLISHED && !live[i]) { live[i] = 1; accepted[i]++; }
            if (st == MEGANET_TCP_ESTABLISHED || st == MEGANET_TCP_CLOSE_WAIT) {
                if (avail) {
                    n = meganet_tcp_recv_s(i, buf, sizeof buf);
                    if (n) echoed[i] = (uint16_t)(echoed[i] + meganet_tcp_send_s(i, buf, n));
                } else if (fl & MEGANET_TCP_F_EOF) {
                    meganet_tcp_close_s(i);          /* the caller hung up */
                }
            } else if (st == MEGANET_TCP_CLOSED && live[i]) {
                live[i] = 0;
                meganet_tcp_listen(i, PORT);         /* next caller */
            }
            ST(20 + i * 2) = (uint8_t)accepted[i]; ST(21 + i * 2) = (uint8_t)(accepted[i] >> 8);
            ST(24 + i * 2) = (uint8_t)echoed[i];   ST(25 + i * 2) = (uint8_t)(echoed[i] >> 8);
        }
        if (us != 0xff) {
            got = meganet_udp_recv(us, buf, sizeof buf, &len, sip, &sport);
            if (got && len) {
                while (meganet_udp_send(us, sip, sport, buf, len) == MEGANET_SEND_PENDING)
                    meganet_poll();
                udp_echoed++;
                ST(28) = (uint8_t)udp_echoed;
            }
        }
        ST(30) = (uint8_t)frames; ST(31) = (uint8_t)(frames >> 8);
        if (frames > 6000) break;                    /* two minutes */
    }
    ST(32) = 'E'; ST(33) = 'N'; ST(34) = 'D';
    for (;;) meganet_poll();
}
