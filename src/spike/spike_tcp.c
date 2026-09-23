/* Step 5 acceptance: two TCP fetches through the table. First a server
 * on the development host (192.168.1.232:7070) that sends 5,000 known bytes
 * -- more than the 4 KB ring, so the window path runs. Then a real
 * gopher server on the internet, resolved by DNS. Status at $1400. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
static const uint8_t mac_ip[4] = { 192,168,1,232 };
static const char gopher_host[] = "gopherpedia.com";
static uint8_t gopher_ip[4];
static uint8_t buf[1024];
static uint16_t loops;

/* Fetch: connect, send req, read until EOF, close. Records at base:
 * +0 final state, +1 flags, +2..3 bytes, +4..5 sum16, +6 max avail,
 * +7 result (1 ok), +8..9 loops, +10..11 recv calls */
static void fetch(uint8_t base, const uint8_t *ip, uint16_t port, const char *req, uint8_t reqlen)
{
    uint8_t st, fl, phase = 0, result = 0; uint16_t avail, n, i, total = 0, sum = 0, calls = 0, maxav = 0;
    uint16_t t0 = 0; uint8_t last = MN_FRAMECOUNT; uint16_t frames = 0;
    meganet_tcp_connect(ip, port);
    for (;;) {
        loops++;
        meganet_poll();
        if (MN_FRAMECOUNT != last) { last = MN_FRAMECOUNT; frames++; }
        st = meganet_tcp_state(&fl, &avail);
        if (avail > maxav) maxav = avail;
        if (phase == 0) {
            if (st == MEGANET_TCP_ESTABLISHED) { meganet_tcp_send(req, reqlen); phase = 1; }
            else if (st == MEGANET_TCP_CLOSED) break;              /* refused / timeout */
        } else if (phase == 1) {
            n = meganet_tcp_recv(buf, sizeof buf);
            if (n) { calls++; for (i = 0; i < n; i++) sum = (uint16_t)(sum + buf[i]); total = (uint16_t)(total + n); }
            if (!n && (fl & MEGANET_TCP_F_EOF)) {                  /* drained after the close */
                meganet_tcp_close(); phase = 2; t0 = frames; result = 1;
            }
            if (st == MEGANET_TCP_CLOSED) { result = (uint8_t)(fl & MEGANET_TCP_F_EOF ? 1 : 0); break; }
        } else {
            if (st == MEGANET_TCP_CLOSED || (uint16_t)(frames - t0) > 150) break;
        }
        if (frames > 1500) break;                                   /* 30 s cap */
    }
    ST(base) = st; ST(base + 1) = fl; ST(base + 2) = (uint8_t)total; ST(base + 3) = (uint8_t)(total >> 8);
    ST(base + 4) = (uint8_t)sum; ST(base + 5) = (uint8_t)(sum >> 8); ST(base + 6) = (uint8_t)(maxav > 255 ? 255 : maxav);
    ST(base + 7) = result; ST(base + 8) = (uint8_t)loops; ST(base + 9) = (uint8_t)(loops >> 8);
    ST(base + 10) = (uint8_t)calls; ST(base + 11) = (uint8_t)(calls >> 8);
    ST(base + 12) = (uint8_t)frames; ST(base + 13) = (uint8_t)(frames >> 8);
}

int main(void)
{
    uint8_t i, st;
    for (i = 0; i < 64; i++) ST(i) = 0;
    ST(0) = 'T'; ST(1) = 'C';
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    meganet_dhcp_start();
    __asm__ volatile("cli");
    do { meganet_poll(); st = meganet_dhcp_state(); } while (st != MEGANET_DHCP_BOUND && st != MEGANET_DHCP_FAILED);
    ST(2) = st;
    if (st != MEGANET_DHCP_BOUND) { ST(3) = 0xF1; for (;;) { } }
    fetch(16, mac_ip, 7070, "/\r\n", 3);                           /* LAN server */
    ST(3) = 1;
    meganet_dns_start(gopher_host);
    do { meganet_poll(); st = meganet_dns_state(); } while (st == MEGANET_DNS_WAITING);
    ST(4) = st;
    if (st == MEGANET_DNS_DONE) {
        meganet_dns_result(gopher_ip);
        for (i = 0; i < 4; i++) ST(8 + i) = gopher_ip[i];
        fetch(32, gopher_ip, 70, "/\r\n", 3);                       /* the internet */
    }
    ST(3) = 0xEE;
    for (;;) { }
}
