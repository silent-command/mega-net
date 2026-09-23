#include "mn_dma.h"
#include "mn_m65.h"

/* Enhanced DMA job, F018B form, in the layout mega65-libc's struct
 * dmagic_dmalist spells out. Its own location is
 * given to the controller through $D702/$D704, so it works from bank 0
 * and from the bank-4 stack image alike. */
/* Kept out of zero page by naming its section: the list address is given
 * to the controller with MN_PHYS_BASE's bank, and zero page is bank 0. */
static volatile uint8_t job[18] __attribute__((section(".bss.mn_dma_job")));

void mn_dma_copy(uint32_t src, uint32_t dst, uint16_t count)
{
    uint16_t list = (uint16_t)(uintptr_t)job;
    /* The list address registers belong to whoever called us. BASIC 65
     * sets $D701/$D702 once and then triggers each of its own jobs by
     * writing $D700 alone, so leaving ours behind sent its next screen
     * scroll to fetch a job list from bank 4 and hung the machine
     * (REQUIREMENTS.md 5.10). Save them, restore them. */
    uint8_t save_msb = MN_PEEK(0xD701);
    uint8_t save_bank = MN_PEEK(0xD702);
    uint8_t save_mb = MN_PEEK(0xD704);

    job[0]  = 0x80;                             /* source bits 20-27 */
    job[1]  = (uint8_t)(src >> 20);
    job[2]  = 0x81;                             /* dest bits 20-27 */
    job[3]  = (uint8_t)(dst >> 20);
    job[4]  = 0x00;                             /* end of options */
    job[5]  = 0x00;                             /* command: copy */
    job[6]  = (uint8_t)(count & 0xff);
    job[7]  = (uint8_t)(count >> 8);
    job[8]  = (uint8_t)(src & 0xff);
    job[9]  = (uint8_t)((src >> 8) & 0xff);
    job[10] = (uint8_t)((src >> 16) & 0x0f);    /* source bank */
    job[11] = (uint8_t)(dst & 0xff);
    job[12] = (uint8_t)((dst >> 8) & 0xff);
    job[13] = (uint8_t)((dst >> 16) & 0x0f);    /* dest bank */
    job[14] = 0x00;                             /* sub-command */
    job[15] = 0x00;                             /* modulo */
    job[16] = 0x00;

    /* The job list itself is in our own memory, so its bank must be told
     * to the controller too. */
    MN_POKE(0xD702, (uint8_t)(MN_PHYS_BASE >> 16));
    MN_POKE(0xD704, (uint8_t)(MN_PHYS_BASE >> 20));
    MN_POKE(0xD701, (uint8_t)(list >> 8));
    MN_POKE(0xD705, (uint8_t)(list & 0xff));    /* triggers enhanced DMA */

    MN_POKE(0xD702, save_bank);                 /* clears $D704 -- so first */
    MN_POKE(0xD704, save_mb);
    MN_POKE(0xD701, save_msb);
}
