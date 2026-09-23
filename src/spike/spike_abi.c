/* R-13 acceptance spike: the step 2 experiment again, but every frame
 * goes through the banked stack and its jump table instead of a driver
 * linked into this program.
 *
 * This program is a plain bank-0 PRG. It carries meganet.bin and the
 * trampoline as arrays, DMAs them into place, and from then on only ever
 * calls $160A. If the ARP-table witness from step 2 reproduces, the whole
 * chain is proven: image in bank 4, MAP, jump table, register mailboxes,
 * zero-page save/restore, 28-bit parameter blocks, DMA both ways.
 *
 * Status block at $1400 as before, plus what the ABI answered.
 */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"
#include "../abi/meganet.h"
#include "meganet_payload.h"

#define ST_BASE     0x1400
#define ST_MAGIC    0       /* 'M','N' */
#define ST_STATE    2
#define ST_TXCOUNT  3
#define ST_RXCOUNT  4
#define ST_LOADCHK  5       /* byte read back from $42000: expect $4C */
#define ST_TRCHK    6       /* byte read back from $160A: expect $78 (sei) */
#define ST_MAC      7       /* 6 bytes, via GET_MAC */
#define ST_TXFAIL   13
#define ST_RXLEN    14
#define ST_RXFRAME  16      /* 64 bytes */
#define ST_LOOPS    80
#define ST_VER      81      /* 4 bytes: A X Y Z from VERSION */
#define ST_INITRC   85      /* A from INIT */
#define ST_P_BEFORE 86      /* processor status before any ABI call */
#define ST_P_AFTER  87      /* ...after INIT returned; bit 2 = I */
#define ST_P_LOOP   88      /* ...after a LINK_RX in the main loop */
#define ST_JIFFY    89      /* 3 bytes: $A0-$A2, KERNAL jiffy clock */
#define ST_LOOPS16  92      /* 2 bytes: main-loop count */
#define ST_FRAMES   94      /* 1 byte: $D7FA */
#define ST_MAPCAP   95      /* 4 bytes: captured map A X Y Z */
#define ST_TRCHK2   99      /* byte at $160E */
#define ST_VEC      100     /* $FFFA,$FFFB,$E000,$01 after KERNAL-to-RAM */

uint8_t mn_get_p(void);
void mn_irq_hook(void);
extern volatile uint8_t mn_irq_orig[2];
#define ST_IRQEN    104     /* $D01A, $DC0E, $D012 before cli */
#define ST_IRQCNT   144     /* $1490: IRQs seen by the hook */

#define STATE_START   1
#define STATE_LOADED  2
#define STATE_INITED  3
#define STATE_SENT    4
#define STATE_GOT_RX  5

#define ST(off) (*(volatile uint8_t *)(ST_BASE + (off)))

static const uint8_t host_ip[4]  = { 192, 168, 1, 232 };
/* .198, not step 2's .199: that one is already in the host's ARP
 * cache from the earlier spike, and a cached entry would make the
 * transmit witness a false positive. A fresh address is a fresh test. */
static const uint8_t spare_ip[4] = { 192, 168, 1, 196 };

static uint8_t mac[6];
static uint8_t frame[1518];
static uint8_t peek1;

static uint16_t build_arp_request(uint8_t *f)
{
    uint8_t i;
    for (i = 0; i < 6; i++) f[i] = 0xff;
    for (i = 0; i < 6; i++) f[6 + i] = mac[i];
    f[12] = 0x08; f[13] = 0x06;
    f[14] = 0x00; f[15] = 0x01;
    f[16] = 0x08; f[17] = 0x00;
    f[18] = 6;    f[19] = 4;
    f[20] = 0x00; f[21] = 0x01;
    for (i = 0; i < 6; i++) f[22 + i] = mac[i];
    for (i = 0; i < 4; i++) f[28 + i] = spare_ip[i];
    for (i = 0; i < 6; i++) f[32 + i] = 0;
    for (i = 0; i < 4; i++) f[38 + i] = host_ip[i];
    return 42;
}

int main(void)
{
    uint16_t n, i;
    uint8_t last, seen = 0, loops = 0;
    uint16_t loops16 = 0;

    ST(ST_MAGIC) = 'M'; ST(ST_MAGIC + 1) = 'N';
    ST(ST_STATE) = STATE_START;
    ST(ST_TXCOUNT) = 0; ST(ST_RXCOUNT) = 0; ST(ST_TXFAIL) = 0;
    ST(ST_RXLEN) = 0; ST(ST_RXLEN + 1) = 0;

    ST(ST_P_BEFORE) = mn_get_p();
    mn_m65_io_enable();

    /* Put the stack and the trampoline where they live, then read one
     * byte of each back: bank addressing has silently done nothing
     * before (gopher, bank 6). */
    mn_dma_copy(MN_PHYS(meganet_bin), MEGANET_BASE, MEGANET_BIN_SIZE);
    mn_dma_copy(MN_PHYS(meganet_call_bin), MEGANET_TR, MEGANET_CALL_BIN_SIZE);
    mn_dma_copy(MEGANET_BASE, MN_PHYS(&peek1), 1);
    ST(ST_LOADCHK) = peek1;
    mn_dma_copy(0x160AUL, MN_PHYS(&peek1), 1);
    ST(ST_TRCHK) = peek1;
    mn_dma_copy(0x160eUL, MN_PHYS(&peek1), 1);
    ST(ST_TRCHK2) = peek1;
    ST(ST_STATE) = STATE_LOADED;

    ST(ST_MAPCAP) = MEGANET_TR_MAP_A; ST(ST_MAPCAP + 1) = MEGANET_TR_MAP_X;
    ST(ST_MAPCAP + 2) = MEGANET_TR_MAP_Y; ST(ST_MAPCAP + 3) = MEGANET_TR_MAP_Z;

    meganet_call(MEGANET_VERSION, 0, 0, 0, 0);
    ST(ST_VER) = MEGANET_TR_RA; ST(ST_VER + 1) = MEGANET_TR_RX;
    ST(ST_VER + 2) = MEGANET_TR_RY; ST(ST_VER + 3) = MEGANET_TR_RZ;

    ST(ST_INITRC) = meganet_call(MEGANET_INIT, 0, 0, 0, 0);
    ST(ST_P_AFTER) = mn_get_p();
    meganet_call_ptr(MEGANET_GET_MAC, mac);
    for (i = 0; i < 6; i++)
        ST(ST_MAC + i) = mac[i];
    ST(ST_STATE) = STATE_INITED;

    /* The real test: run the rest with interrupts ENABLED. llvm-mos's
     * crt0 leaves the C65 KERNAL mapped at $E000 (MAPHI $83) but masks
     * interrupts; the stack calls preserve that map, so a bare cli is
     * all it takes. If the jiffy clock then advances, the KERNAL IRQ is
     * running while the stack is in use -- the R-19 goal. */
    /* The trampoline restores the caller's map after every call, KERNAL
     * included; record the vectors to show it is there. */
    ST(ST_VEC) = MN_PEEK(0xFFFA); ST(ST_VEC + 1) = MN_PEEK(0xFFFB);
    ST(ST_VEC + 2) = MN_PEEK(0xE000); ST(ST_VEC + 3) = MN_PEEK(0x01);
    ST(ST_IRQEN) = MN_PEEK(0xD01A); ST(ST_IRQEN + 1) = MN_PEEK(0xDC0E);
    ST(ST_IRQEN + 2) = MN_PEEK(0xD012);
    ST(ST_IRQCNT) = 0;
    mn_irq_orig[0] = MN_PEEK(0x0314); mn_irq_orig[1] = MN_PEEK(0x0315);
    MN_POKE(0x0314, (uint8_t)((uint16_t)(uintptr_t)mn_irq_hook & 0xff));
    MN_POKE(0x0315, (uint8_t)((uint16_t)(uintptr_t)mn_irq_hook >> 8));
    __asm__ volatile("cli");

    last = MN_FRAMECOUNT;
    for (;;) {
        ST(ST_LOOPS) = ++loops;
        loops16++;
        ST(ST_LOOPS16) = (uint8_t)(loops16 & 0xff);
        ST(ST_LOOPS16 + 1) = (uint8_t)(loops16 >> 8);
        ST(ST_JIFFY) = MN_PEEK(0xA0);
        ST(ST_JIFFY + 1) = MN_PEEK(0xA1);
        ST(ST_JIFFY + 2) = MN_PEEK(0xA2);
        ST(ST_FRAMES) = MN_FRAMECOUNT;

        if (MN_FRAMECOUNT != last) {
            last = MN_FRAMECOUNT;
            if (++seen >= 100) {
                seen = 0;
                n = build_arp_request(frame);
                if (meganet_link_tx(frame, n)) {
                    ST(ST_TXCOUNT)++;
                    if (ST(ST_STATE) < STATE_SENT)
                        ST(ST_STATE) = STATE_SENT;
                } else {
                    ST(ST_TXFAIL)++;
                }
            }
        }

        if (meganet_link_rx(frame, sizeof frame, &n)) {
            ST(ST_P_LOOP) = mn_get_p();
            ST(ST_RXCOUNT)++;
            ST(ST_RXLEN) = (uint8_t)(n & 0xff);
            ST(ST_RXLEN + 1) = (uint8_t)(n >> 8);
            for (i = 0; i < 64 && i < n; i++)
                ST(ST_RXFRAME + i) = frame[i];
            ST(ST_STATE) = STATE_GOT_RX;
        }
    }
}
