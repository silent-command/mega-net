/* Lease renewal against the real router, without waiting half a lease:
 * once bound, the lease clock inside the image is set to a minute before
 * T1, then T2, then expiry, by DMA into the DHCP state (its layout comes
 * from the same header the image was built with). Each step records what
 * the module did. Status at $1400; see the ST() offsets. About three
 * minutes on hardware. REQUIREMENTS.md 5.14. */
#include <stdint.h>
#include <stddef.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "../net/mn_dhcp.h"
#include "meganet_payload.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
#define DHCP_PHYS (MEGANET_BASE - 0x2000UL + MEGANET_DHCP_STATE_ADDR)

static uint16_t rd16(size_t off)
{
    uint16_t v;
    mn_dma_copy(DHCP_PHYS + off, MN_PHYS(&v), 2);
    return v;
}
static void wr16(size_t off, uint16_t v)
{
    mn_dma_copy(MN_PHYS(&v), DHCP_PHYS + off, 2);
}

static uint8_t last;
static uint16_t frames;
static void pump(void)
{
    meganet_poll();
    if (MN_FRAMECOUNT != last) { last = MN_FRAMECOUNT; frames++; }
}

/* Polls until the DHCP phase (or state) matches, or `limit` frames pass.
 * Returns the frames it took, or 0xFFFF. */
static uint16_t wait_for(uint8_t want_state, uint8_t want_phase, uint16_t limit)
{
    uint16_t t0 = frames;
    uint8_t st, ph;
    for (;;) {
        pump();
        st = meganet_dhcp_status(&ph, 0);
        if (st == want_state && (want_phase == 0xff || ph == want_phase))
            return (uint16_t)(frames - t0);
        if ((uint16_t)(frames - t0) > limit)
            return 0xFFFF;
    }
}

int main(void)
{
    uint8_t i, ph;
    uint16_t left, t1, t2, lease, took;
    meganet_stats_t s;

    for (i = 0; i < 64; i++) ST(i) = 0;
    ST(0) = 'R'; ST(1) = 'N';
    mn_m65_io_enable();
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    last = MN_FRAMECOUNT;
    meganet_dhcp_start();
    took = wait_for(MEGANET_DHCP_BOUND, 0xff, 1500);
    ST(2) = (uint8_t)took; ST(3) = (uint8_t)(took >> 8);
    if (took == 0xFFFF) { ST(4) = 'F'; for (;;) pump(); }

    meganet_dhcp_status(&ph, &left);
    t1 = rd16(offsetof(mn_dhcp, t1_m)); t2 = rd16(offsetof(mn_dhcp, t2_m)); lease = rd16(offsetof(mn_dhcp, lease_m));
    ST(4) = (uint8_t)left; ST(5) = (uint8_t)(left >> 8);         /* lease left, minutes */
    ST(6) = (uint8_t)t1; ST(7) = (uint8_t)(t1 >> 8);
    ST(8) = (uint8_t)t2; ST(9) = (uint8_t)(t2 >> 8);
    ST(10) = (uint8_t)lease; ST(11) = (uint8_t)(lease >> 8);

    /* 1. Renewal: a minute before T1, then within ~a minute the module
     *    must send its unicast REQUEST and the router must ACK. */
    wr16(offsetof(mn_dhcp, age_m), (uint16_t)(t1 - 1));
    took = wait_for(MEGANET_DHCP_BOUND, 1, 4000);                 /* renewing */
    ST(12) = (uint8_t)took; ST(13) = (uint8_t)(took >> 8);
    took = wait_for(MEGANET_DHCP_BOUND, 0, 1500);                 /* bound again = ACK */
    ST(14) = (uint8_t)took; ST(15) = (uint8_t)(took >> 8);
    meganet_get_stats(&s); ST(40) = s.dhcp_last_type; ST(41) = s.dhcp_rx[0];
    meganet_dhcp_status(&ph, &left);
    ST(16) = (uint8_t)left; ST(17) = (uint8_t)(left >> 8);         /* full again? */

    /* 2. Rebinding: a minute before T2. */
    wr16(offsetof(mn_dhcp, age_m), (uint16_t)(t2 - 1));
    took = wait_for(MEGANET_DHCP_BOUND, 2, 4000);
    ST(18) = (uint8_t)took; ST(19) = (uint8_t)(took >> 8);
    took = wait_for(MEGANET_DHCP_BOUND, 0, 1500);
    ST(20) = (uint8_t)took; ST(21) = (uint8_t)(took >> 8);
    meganet_get_stats(&s); ST(42) = s.dhcp_last_type; ST(43) = s.dhcp_rx[0];

    /* 3. Expiry: a minute before the end; the address must be dropped and
     *    re-acquired. */
    wr16(offsetof(mn_dhcp, age_m), (uint16_t)(lease - 1));
    took = wait_for(MEGANET_DHCP_SELECTING, 0xff, 4000);
    ST(22) = (uint8_t)took; ST(23) = (uint8_t)(took >> 8);
    meganet_get_stats(&s); ST(44) = s.dhcp_last_type; ST(45) = s.dhcp_rx[0];
    { meganet_ipconf_t c = {{0,0,0,0},{0,0,0,0},{0,0,0,0}}; meganet_get_ip(&c); ST(24) = c.ip[3]; }  /* 0 while selecting */
    took = wait_for(MEGANET_DHCP_BOUND, 0xff, 1500);
    ST(26) = (uint8_t)took; ST(27) = (uint8_t)(took >> 8);
    { meganet_ipconf_t c = {{0,0,0,0},{0,0,0,0},{0,0,0,0}}; meganet_get_ip(&c); ST(28) = c.ip[3]; }

    meganet_get_stats(&s);
    ST(30) = s.dhcp_tries[0]; ST(31) = s.dhcp_rx[0];
    ST(32) = 'E'; ST(33) = 'N'; ST(34) = 'D';
    for (;;) pump();
}
