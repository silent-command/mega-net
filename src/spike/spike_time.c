/* Step 4 acceptance, second half: DHCP, then resolve pool.ntp.org, then
 * SNTP, then set the RTC. Status at $1400. UTC offset of the host, in
 * minutes, baked in at build time: -240. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
#define OFFSET_MIN (-240)
static const char host[] = "pool.ntp.org";
static uint8_t ntp_ip[4];
static meganet_time_t t;
static meganet_rtc_t r;
static uint8_t rtc[8];
static uint16_t loops;

int main(void)
{
    uint8_t i, st, phase = 0;
    for (i = 0; i < 64; i++) ST(i) = 0;
    ST(0) = 'T'; ST(1) = 'M';
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    meganet_dhcp_start();
    __asm__ volatile("cli");
    for (;;) {
        loops++; ST(8) = (uint8_t)loops; ST(9) = (uint8_t)(loops >> 8);
        meganet_poll();
        if (phase == 0) {
            st = meganet_dhcp_state(); ST(3) = st;
            if (st == MEGANET_DHCP_BOUND) { phase = 1; ST(4) = meganet_dns_start(host); }
            else if (st == MEGANET_DHCP_FAILED) { ST(2) = 0xF1; for (;;) { } }
        } else if (phase == 1) {
            st = meganet_dns_state(); ST(5) = st;
            if (st == MEGANET_DNS_DONE) {
                meganet_dns_result(ntp_ip);
                for (i = 0; i < 4; i++) ST(16 + i) = ntp_ip[i];
                phase = 2; meganet_ntp_start(ntp_ip);
            } else if (st == MEGANET_DNS_FAILED) { ST(2) = 0xF2; for (;;) { } }
        } else if (phase == 2) {
            st = meganet_ntp_state(); ST(6) = st;
            if (st == MEGANET_NTP_DONE) {
                t.offset_min[0] = (uint8_t)(OFFSET_MIN & 0xff);
                t.offset_min[1] = (uint8_t)((OFFSET_MIN >> 8) & 0xff);
                meganet_ntp_result(&t);
                for (i = 0; i < 14; i++) ST(20 + i) = ((uint8_t *)&t)[i];
                r.year[0] = t.year[0]; r.year[1] = t.year[1];
                r.month = t.month; r.day = t.day; r.hour = t.hour;
                r.minute = t.minute; r.second = t.second; r.weekday = t.weekday;
                ST(7) = meganet_set_rtc(&r);
                mn_dma_copy(0xFFD7110UL, MN_PHYS(rtc), 7);
                for (i = 0; i < 7; i++) ST(40 + i) = rtc[i];
                phase = 3; ST(2) = 0xEE;
            } else if (st == MEGANET_NTP_FAILED) { ST(2) = 0xF3; for (;;) { } }
        }
    }
}
