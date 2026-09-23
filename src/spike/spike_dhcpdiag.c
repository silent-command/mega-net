/* Time series of the stack's counters around DHCP_START, one 16-byte
 * sample per second at $1420 + 16*n (n < 24), plus the DHCP state. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"
#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
static meganet_stats_t stats;
int main(void)
{
    uint8_t i, n = 0, last, seen = 0;
    for (i = 0; i < 32; i++) ST(i) = 0;
    ST(0) = 'D'; ST(1) = 'G';
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    ST(2) = meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    __asm__ volatile("cli");
    last = MN_FRAMECOUNT;
    for (;;) {
        meganet_poll();
        if (MN_FRAMECOUNT != last) {
            last = MN_FRAMECOUNT;
            if (++seen >= 50 && n < 12) {
                seen = 0;
                meganet_get_stats(&stats);
                for (i = 0; i < 32; i++) ST(32 + n * 34 + i) = ((uint8_t *)&stats)[i];
                ST(32 + n * 34 + 32) = meganet_dhcp_state();
                ST(32 + n * 34 + 33) = 0xAA;
                n++;
                ST(3) = n;
                if (n == 6)                         /* after the first OFFER */
                    meganet_call_ptr(MEGANET_DHCP_LAST_MSG, (void *)0x1700);   /* clear of the samples */
                if (n == 3) {                      /* three quiet seconds, then DHCP */
                    ST(4) = meganet_dhcp_start();
                    meganet_call_ptr(MEGANET_DHCP_LAST_MSG, (void *)0x1900);  /* our DISCOVER */
                }
            }
        }
    }
}
