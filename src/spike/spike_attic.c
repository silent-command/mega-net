/* Attic RAM under DMA: when do reads come back stale? For each value of
 * the cache register $BFFFFF2 (left alone, then $00 $20 $40 $60 $80 $E0):
 *   A. write 4 KB by DMA, read it all back: mismatches;
 *   B. the ring's pattern -- write 256 bytes at chunk k, immediately read
 *      chunks k-1 and k back: mismatches over 16 chunks;
 *   C. write N bytes at one address and read them straight back, for
 *      N = 1, 8, 32, 256, 1024: mismatches.
 * Status at $1400: 8 bytes per register value: value, A lo/hi, B lo/hi,
 * C bits (bit n set = size n failed), 0, 0. REQUIREMENTS.md 5.16. */
#include <stdint.h>
#include "../hal/mn_m65.h"
#include "../hal/mn_dma.h"

#define ST(o) (*(volatile uint8_t *)(0x1400 + (o)))
#ifndef ATTIC
#define ATTIC 0x8000000UL       /* -DATTIC=0x58000UL for the bank-5 control run */
#endif
#define CACHE_REG 0xBFFFFF2UL
static uint8_t out[1024], in[1024];

static void set_reg(uint8_t v) { mn_dma_copy(MN_PHYS(&v), CACHE_REG, 1); }
static uint8_t get_reg(void) { uint8_t v; mn_dma_copy(CACHE_REG, MN_PHYS(&v), 1); return v; }

static uint16_t test_a(uint8_t salt)
{
    uint16_t i, bad = 0, k;
    for (k = 0; k < 4; k++) {
        for (i = 0; i < 1024; i++) out[i] = (uint8_t)(i * 7 + salt + k);
        mn_dma_copy(MN_PHYS(out), ATTIC + k * 1024UL, 1024);
    }
    for (k = 0; k < 4; k++) {
        mn_dma_copy(ATTIC + k * 1024UL, MN_PHYS(in), 1024);
        for (i = 0; i < 1024; i++) if (in[i] != (uint8_t)(i * 7 + salt + k)) bad++;
    }
    return bad;
}

static uint16_t test_b(uint8_t salt)
{
    uint16_t i, bad = 0, k;
    for (k = 0; k < 16; k++) {
        for (i = 0; i < 256; i++) out[i] = (uint8_t)(i * 3 + salt + k * 11);
        mn_dma_copy(MN_PHYS(out), ATTIC + k * 256UL, 256);
        if (k) {
            mn_dma_copy(ATTIC + (k - 1) * 256UL, MN_PHYS(in), 256);
            for (i = 0; i < 256; i++) if (in[i] != (uint8_t)(i * 3 + salt + (k - 1) * 11)) bad++;
        }
        mn_dma_copy(ATTIC + k * 256UL, MN_PHYS(in), 256);
        for (i = 0; i < 256; i++) if (in[i] != (uint8_t)(i * 3 + salt + k * 11)) bad++;
    }
    return bad;
}

static uint8_t test_c(uint8_t salt)
{
    static const uint16_t sizes[5] = { 1, 8, 32, 256, 1024 };
    uint8_t bits = 0, n;
    uint16_t i;
    for (n = 0; n < 5; n++) {
        for (i = 0; i < sizes[n]; i++) out[i] = (uint8_t)(i * 5 + salt + n);
        mn_dma_copy(MN_PHYS(out), ATTIC + 0x800UL, sizes[n]);
        mn_dma_copy(ATTIC + 0x800UL, MN_PHYS(in), sizes[n]);
        for (i = 0; i < sizes[n]; i++) if (in[i] != (uint8_t)(i * 5 + salt + n)) { bits |= (uint8_t)(1 << n); break; }
    }
    return bits;
}

int main(void)
{
    static const uint8_t regs[7] = { 0xFF, 0x00, 0x20, 0x40, 0x60, 0x80, 0xE0 };
    uint8_t r, before, i;
    uint16_t a, b;
    for (i = 0; i < 64; i++) ST(i) = 0;
    mn_m65_io_enable();
    before = get_reg();
    ST(60) = 'A'; ST(61) = 'T'; ST(62) = before;
    for (r = 0; r < 7; r++) {
        if (regs[r] != 0xFF) set_reg(regs[r]);
        a = test_a((uint8_t)(r * 17));
        b = test_b((uint8_t)(r * 29));
        ST(r * 8 + 0) = regs[r] == 0xFF ? before : regs[r];
        ST(r * 8 + 1) = (uint8_t)a; ST(r * 8 + 2) = (uint8_t)(a >> 8);
        ST(r * 8 + 3) = (uint8_t)b; ST(r * 8 + 4) = (uint8_t)(b >> 8);
        ST(r * 8 + 5) = test_c((uint8_t)(r * 43));
        ST(r * 8 + 6) = get_reg();
    }
    set_reg(before);
    ST(63) = 'E';
    for (;;) ;
}
