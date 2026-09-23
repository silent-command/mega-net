/* Link script for the mega-net stack image: a raw, headerless binary
 * that lives in bank 4 and is executed with bank 4 mapped over CPU
 * $2000-$BFFF. It is loaded at physical $42000 by
 * `BLOAD "meganet.bin",P($42000)` from BASIC, or by a DMA copy from a C
 * client, and never touches the load address itself.
 *
 * $2000-$BBFF is code and data. $BC00-$BFFF is the stack's own soft
 * stack (see __stack); the prologue in jumptable.S switches to it on
 * entry so the caller's soft stack is never used while their memory is
 * hidden. A 512-byte stack at the top of a 24 KB window overflowed into
 * the mailboxes once the image filled the window (REQUIREMENTS.md 5.7). */

/* $2000-$BBFF: 40 KB, mapped from physical $42000-$4BBFF. The trampoline
 * maps both the low window ($2000-$7FFF, MAPLO) and $8000-$BFFF (MAPHI,
 * offset $40000) during a call. $BC00-$BFFF is the soft stack. */
MEMORY {
    ram (rw) : ORIGIN = 0x2000, LENGTH = 0x9C00
}

/* The stack's zero page is $90-$FF -- deliberately NOT the $02-$8F that
 * every llvm-mos program (and BASIC's own workspace below $90) uses.
 * Keeping it disjoint is what lets the stack DMA results into a caller's
 * memory even when the compiler has put that buffer in zero page: an
 * earlier design saved and restored the caller's $02-$8F around each
 * call, and silently reverted exactly such a result (REQUIREMENTS.md
 * 5.3). $90-$FF belongs to BASIC/KERNAL, which is not running during a
 * call (the entry disables interrupts); jumptable.S saves and restores
 * the whole range so BASIC survives. 32 bytes are the imaginary
 * registers; the 80 after them are the compiler's .zp. */
__rc0 = 0x90;
INCLUDE imag-regs.ld
MEMORY { zp : ORIGIN = __rc31 + 1, LENGTH = 0x100 - (__rc31 + 1) }

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

__stack = 0xC000;

SECTIONS {
    /* The jump table is the ABI. It must be the first bytes of the image
     * so that entry N is at $42000 + 3N forever. */
    .jumptable : { KEEP(*(.jumptable)) } > ram
    INCLUDE c.ld
}

OUTPUT_FORMAT { TRIM(ram) }
