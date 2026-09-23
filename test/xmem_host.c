/* The socket-buffer pool on the host: an array standing in for attic RAM.
 * Addresses outside the pool are a test bug, and abort. The 28-bit side
 * of copy_in/copy_out is a pool address too on the host. */
#include "../src/net/mn_xmem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define POOL_SIZE 0x10000UL
static uint8_t pool[POOL_SIZE];

static uint8_t *at(uint32_t addr, uint16_t len)
{
    if (addr < MN_XMEM_BASE || addr + len > MN_XMEM_BASE + POOL_SIZE) {
        fprintf(stderr, "xmem: $%07lx+%u is outside the pool\n", (unsigned long)addr, len);
        abort();
    }
    return pool + (addr - MN_XMEM_BASE);
}
#define ADDR(seg, off) (((uint32_t)(seg) << 16) | (off))

void mn_xmem_read(void *dst, uint16_t seg, uint16_t off, uint16_t len) { memcpy(dst, at(ADDR(seg, off), len), len); }
void mn_xmem_write(uint16_t seg, uint16_t off, const void *src, uint16_t len) { memcpy(at(ADDR(seg, off), len), src, len); }
void mn_xmem_copy_out(uint32_t dst, uint16_t seg, uint16_t off, uint16_t len) { memmove(at(dst, len), at(ADDR(seg, off), len), len); }
void mn_xmem_copy_in(uint16_t seg, uint16_t off, uint32_t src, uint16_t len) { memmove(at(ADDR(seg, off), len), at(src, len), len); }
