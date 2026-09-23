/* DMAgic copy between 28-bit addresses.
 *
 * The 45E100's frame buffer lives at $FFDE800, outside anything the 6502
 * can address directly, so DMA is part of the link layer rather than an
 * optimisation of it. This is deliberately not mega65-libc's lcopy(): a
 * sibling-checkout dependency built with cmake is exactly the kind of
 * host-specific step R-17 forbids, and the job is forty lines.
 */
#ifndef MN_DMA_H
#define MN_DMA_H

#include <stdint.h>

/* Copies count bytes from src to dst, both 28-bit addresses. Requires the
 * I/O personality to be enabled. Not reentrant. */
void mn_dma_copy(uint32_t src, uint32_t dst, uint16_t count);

#endif
