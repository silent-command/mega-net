#include "../net/mn_xmem.h"
#include "mn_m65.h"
#include "mn_dma.h"

#define ADDR(seg, off) (((uint32_t)(seg) << 16) | (off))

void mn_xmem_read(void *dst, uint16_t seg, uint16_t off, uint16_t len)
{
    if (len) mn_dma_copy(ADDR(seg, off), MN_PHYS(dst), len);
}

void mn_xmem_write(uint16_t seg, uint16_t off, const void *src, uint16_t len)
{
    if (len) mn_dma_copy(MN_PHYS(src), ADDR(seg, off), len);
}

void mn_xmem_copy_out(uint32_t dst, uint16_t seg, uint16_t off, uint16_t len)
{
    if (len) mn_dma_copy(ADDR(seg, off), dst, len);
}

void mn_xmem_copy_in(uint16_t seg, uint16_t off, uint32_t src, uint16_t len)
{
    if (len) mn_dma_copy(src, ADDR(seg, off), len);
}
