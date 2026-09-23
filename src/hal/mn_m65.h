/* MEGA65 target-side primitives. Target only -- never included by src/net/.
 *
 * Everything here is a memory-mapped register or a fixed idiom carried
 * over from the gopher project. Nothing is a guess: the register map is
 * the MEGA65 documentation's, the timing idiom is PLATFORM-NOTES.md.
 */
#ifndef MN_M65_H
#define MN_M65_H

#include <stdint.h>

#define MN_POKE(addr, v) (*(volatile uint8_t *)(addr) = (uint8_t)(v))
#define MN_PEEK(addr)    (*(volatile uint8_t *)(addr))

/* Where this binary's own memory is in the 28-bit space. A bank-0 PRG is
 * at 0; the stack image, which runs mapped into $2000-$7FFF but lives at
 * $42000, is built with -DMN_PHYS_BASE=0x40000UL. Any pointer to our own
 * data that is handed to DMA must go through MN_PHYS(), or the DMA
 * quietly reads the wrong bank. */
#ifndef MN_PHYS_BASE
#define MN_PHYS_BASE 0x00000UL
#endif
/* Zero page is always bank 0 -- it is never remapped -- even when the
 * rest of the image lives at $40000. The compiler puts small statics
 * there, and some of them are handed to DMA. */
#define MN_PHYS(p) ((uint16_t)(uintptr_t)(p) < 0x100u \
                    ? (uint32_t)(uint16_t)(uintptr_t)(p) \
                    : MN_PHYS_BASE + (uint32_t)(uint16_t)(uintptr_t)(p))

/* Unlocks the MEGA65 I/O personality so $D6xx registers are visible. */
static inline void mn_m65_io_enable(void)
{
    MN_POKE(0xD02F, 0x47);
    MN_POKE(0xD02F, 0x53);
}

/* $D7FA increments once per video frame (50 or 60 Hz). This, not a VIC-II
 * raster register, is what every wait in mega-net is paced on (R-14). */
#define MN_FRAMECOUNT MN_PEEK(0xD7FA)

/* Waits for `frames` frame-counter ticks. Bounded twice over: by the
 * frame count itself, and by a raw spin cap in case the counter stalls --
 * which has bitten the gopher project before. */
static inline void mn_m65_wait_frames(uint8_t frames)
{
    uint8_t last = MN_FRAMECOUNT;
    uint8_t seen = 0;
    uint8_t wraps = 0;
    uint16_t spins = 0;

    while (seen < frames) {
        uint8_t cur = MN_FRAMECOUNT;
        if (cur != last) {
            last = cur;
            seen++;
        }
        if (++spins == 0 && ++wraps >= 200)
            return;                     /* counter stalled; give up */
    }
}

#endif
