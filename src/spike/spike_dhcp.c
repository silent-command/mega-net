/* Step 4 acceptance, part one: get an address from the LAN's DHCP server
 * and keep answering ping on it. Status at $1400. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
static meganet_ipconf_t conf;
static uint8_t dns[4];
static meganet_stats_t stats;
static uint16_t loops;

int main(void)
{
    uint8_t i, st, last, seen = 0, bound = 0;
    ST(0) = 'D'; ST(1) = 'H'; ST(2) = 0;
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    ST(3) = meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    ST(4) = meganet_dhcp_start();
    ST(2) = 1;
    __asm__ volatile("cli");
    last = MN_FRAMECOUNT;
    for (;;) {
        loops++;
        ST(8) = (uint8_t)loops; ST(9) = (uint8_t)(loops >> 8);
        meganet_poll();
        st = meganet_dhcp_state();
        ST(5) = st;
        if (st == MEGANET_DHCP_BOUND && !bound) {
            bound = 1;
            meganet_get_ip(&conf);
            meganet_get_dns(dns);
            for (i = 0; i < 12; i++) ST(32 + i) = ((uint8_t *)&conf)[i];
            for (i = 0; i < 4; i++) ST(44 + i) = dns[i];
            ST(6) = (uint8_t)loops; ST(7) = (uint8_t)(loops >> 8);   /* when */
            ST(2) = 2;
        }
        if (MN_FRAMECOUNT != last) {
            last = MN_FRAMECOUNT;
            if (++seen >= 50) {
                seen = 0;
                meganet_get_stats(&stats);
                for (i = 0; i < 8; i++) ST(16 + i) = ((uint8_t *)&stats)[i];
            }
        }
    }
}
