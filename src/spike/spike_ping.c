/* Step 3 acceptance: load the stack, give it an address, and poll.
 * The witness is `ping` from the development host. Status at $1400. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
static const meganet_ipconf_t conf = {
    { 192, 168, 1, 195 }, { 255, 255, 255, 0 }, { 192, 168, 1, 1 } };
static meganet_stats_t stats;
static uint16_t loops;

int main(void)
{
    uint8_t i, last, seen = 0;
    ST(0) = 'P'; ST(1) = 'G'; ST(2) = 0;
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    ST(2) = 1;
    ST(3) = meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    ST(4) = meganet_set_ip(&conf);
    ST(2) = 2;
    __asm__ volatile("cli");                     /* the proven mode */
    last = MN_FRAMECOUNT;
    for (;;) {
        loops++;
        ST(8) = (uint8_t)loops; ST(9) = (uint8_t)(loops >> 8);
        meganet_poll();
        if (MN_FRAMECOUNT != last) {             /* stats every ~1 s */
            last = MN_FRAMECOUNT;
            if (++seen >= 50) {
                seen = 0;
                meganet_get_stats(&stats);
                for (i = 0; i < 8; i++) ST(16 + i) = ((uint8_t *)&stats)[i];
                ST(2) = 3;
            }
        }
    }
}
