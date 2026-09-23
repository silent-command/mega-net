/* A C caller's interrupt vectors, taken from the KERNAL. See the note
 * in meganet.h and REQUIREMENTS.md 5.18. Uses lpoke and lcopy from
 * mega65-libc, which every llvm-mos client here links. */
#include <stdint.h>
#include "mega65/memory.h"
#include "meganet.h"

#define STUB_AT 0x0FF00UL             /* bank 0 under $E000: unreachable while the KERNAL is mapped, so by DMA */
#define REC_LO 0x80                  /* the record, $FF80-$FF82, as the two bytes the stub needs */
#define REC_HI 0xFF

/* PHA; LDA rec+1; BNE done; TSX; LDA $103,X; STA rec; LDA $104,X;
 * STA rec+1; LDA $102,X; STA rec+2; done: PLA; RTI. Records the first
 * interrupted PC and flags at $FF80-$FF82, then returns; every later
 * event just returns. */
static unsigned char stub[27] = {
  0x48, 0xAD, REC_LO + 1, REC_HI, 0xD0, 0x13, 0xBA,
  0xBD, 0x03, 0x01, 0x8D, REC_LO, REC_HI,
  0xBD, 0x04, 0x01, 0x8D, REC_LO + 1, REC_HI,
  0xBD, 0x02, 0x01, 0x8D, REC_LO + 2, REC_HI,
  0x68, 0x40
};

void meganet_own_vectors(void)
{
  lcopy((long)(unsigned int)stub, (long)STUB_AT, sizeof stub);
  lpoke(STUB_AT + 0xFA, 0x00); lpoke(STUB_AT + 0xFB, 0xFF);     /* NMI */
  lpoke(STUB_AT + 0xFC, 0x00); lpoke(STUB_AT + 0xFD, 0xFF);     /* reset: never taken from here */
  lpoke(STUB_AT + 0xFE, 0x00); lpoke(STUB_AT + 0xFF, 0xFF);     /* IRQ and BRK */
  lpoke(STUB_AT + 0x80, 0); lpoke(STUB_AT + 0x81, 0); lpoke(STUB_AT + 0x82, 0);
  /* $2000-$7FFF identity as BASIC left it, nothing mapped above $8000 */
  __asm__ volatile("lda #0\n\tldx #0xe0\n\tldy #0\n\tldz #0\n\tmap\n\teom" ::: "a", "x", "y", "memory");
  *(volatile uint8_t *)0x01 = 0x3d;                              /* HIRAM off, or $01 brings the C64 KERNAL back at $E000 */
  meganet_set_restore_map(0x00, 0xE0, 0x00, 0x00);
}
