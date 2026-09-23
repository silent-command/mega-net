/* Bulk memory outside the image: the socket buffers live here, so that
 * a socket costs the 40 KB window a few dozen bytes of state rather than
 * five kilobytes of rings (R-15). On the MEGA65 the pool is attic RAM or
 * bank 5, reached by DMA; on the host it is an array. REQUIREMENTS.md 5.11.
 *
 * Addresses are split into a segment (bits 16-27 of the 28-bit address)
 * and a 16-bit offset, so that ring arithmetic stays 16-bit on the 6502:
 * a buffer never crosses a 64 KB boundary. */
#ifndef MN_XMEM_H
#define MN_XMEM_H

#include <stdint.h>

#ifndef MN_XMEM_BASE
#define MN_XMEM_BASE 0x8000000UL    /* the host pool's address; the image
                                       probes for the real one at INIT */
#endif

void mn_xmem_read(void *dst, uint16_t seg, uint16_t off, uint16_t len);        /* pool -> local */
void mn_xmem_write(uint16_t seg, uint16_t off, const void *src, uint16_t len); /* local -> pool */
void mn_xmem_copy_out(uint32_t dst, uint16_t seg, uint16_t off, uint16_t len); /* pool -> 28-bit */
void mn_xmem_copy_in(uint16_t seg, uint16_t off, uint32_t src, uint16_t len);  /* 28-bit -> pool */

#endif
