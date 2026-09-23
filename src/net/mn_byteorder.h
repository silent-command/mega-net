/* Big-endian (network order) accessors.
 *
 * These are byte-at-a-time on purpose. The 6502 is little-endian and
 * llvm-mos will not reliably lay out packed structs the same way a host
 * compiler does, so the stack never overlays a struct on a packet. Every
 * field is read and written through these helpers instead. It costs a
 * little speed and buys portability between the host tests and the target.
 */
#ifndef MN_BYTEORDER_H
#define MN_BYTEORDER_H

#include <stdint.h>

/* For routines that are large and called from many places: link-time
 * optimisation inlines them into every caller otherwise, and a 6502
 * image has no room for eight copies of a segment builder (5.11). */
#if defined(__GNUC__) || defined(__clang__)
#define MN_NOINLINE __attribute__((noinline))
#else
#define MN_NOINLINE
#endif
/* Module state pinned to ordinary memory on the target: llvm-mos promotes
 * small statics to zero page otherwise, and the image's zero page is the
 * 80 bytes it swaps in and out on every call (5.17). Nothing on the host,
 * whose object format spells sections differently. */
#ifdef __mos__
#define MN_BSS(name) __attribute__((section(".bss." name)))
#else
#define MN_BSS(name)
#endif

static inline uint16_t mn_get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline void mn_put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static inline uint32_t mn_get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void mn_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xff);
}

#endif
