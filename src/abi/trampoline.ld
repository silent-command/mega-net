/* The trampoline is a raw binary at $1600: the page the stack reserves in
 * every client (REQUIREMENTS.md 5.10). Pure assembly: no C runtime, no
 * libraries. */
MEMORY { ram (rwx) : ORIGIN = 0x1600, LENGTH = 0x100 }
SECTIONS { .text : { KEEP(*(.text)) } > ram }
OUTPUT_FORMAT { TRIM(ram) }
