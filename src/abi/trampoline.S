; Far-call trampoline for C and assembly callers.
;
; Lives at $1600, in block 0, which is never remapped: the stack reserves
; the page $1600-$16FF in every client. It is inside $1600-$1EFF, which
; BASIC 65 documents as free for programs; $0334, the first home, is the
; KERNAL's own far-call code (REQUIREMENTS.md 5.10). Stage the target and
; the registers, call meganet_tr_call, read the results back. The layout
; is fixed and mirrored in meganet.h.
;
; The map. During the call bank 4 is mapped over $2000-$BFFF: MAPLO
; X=$E4 (blocks 1-3, offset $400 pages) and MAPHI Z=$B4 (blocks 0, 1 and
; 3 of the upper half, offset $40000), so the image has 40 KB and the
; block at $E000 is bank 4's top 8 KB, where the stack keeps the
; interrupt vectors that are in force during a call. The caller's $E000
; KERNAL mapping is put back by the restore, so the caller may run with
; interrupts enabled between calls.
;
; Z IS ZEROED BEFORE RETURNING. llvm-mos compiles every "store zero" as
; STZ, which on the 45GS02 stores the Z register. A MAP sequence leaves
; Z = its last operand; return to C with Z = $83 and every zero the
; compiler writes becomes $83. That one fact cost a day (REQUIREMENTS.md
; 5.4). Any assembly that loads Z must leave it 0 for C.
;
; Interrupts are off across the window (SEI; the caller's I flag is saved
; by PHP and restored by PLP).

        .section .text,"ax",@progbits
        .global meganet_tr_addr, meganet_tr_a, meganet_tr_x
        .global meganet_tr_y, meganet_tr_z
        .global meganet_tr_ra, meganet_tr_rx, meganet_tr_ry, meganet_tr_rz
        .global meganet_tr_map_a, meganet_tr_map_x
        .global meganet_tr_map_y, meganet_tr_map_z
        .global meganet_tr_call

meganet_tr_addr:  .byte 0, 0    ; $1600  entry address as mapped ($2000+3N)
meganet_tr_a:     .byte 0       ; $1602
meganet_tr_x:     .byte 0       ; $1603
meganet_tr_y:     .byte 0       ; $1604
meganet_tr_z:     .byte 0       ; $1605
meganet_tr_ra:    .byte 0       ; $1606
meganet_tr_rx:    .byte 0       ; $1607
meganet_tr_ry:    .byte 0       ; $1608
meganet_tr_rz:    .byte 0       ; $1609
; The map restored after each call, as MAP register arguments. Default:
; a running llvm-mos program's map, measured -- $2000-$7FFF identity to
; bank 0, the C65 KERNAL at $E000 from bank 3. Restoring the KERNAL is
; what lets the caller run with interrupts enabled. A caller that has
; remapped sets these bytes itself.
meganet_tr_map_a: .byte 0x00    ; $160A  MAPLO low  (A)
meganet_tr_map_x: .byte 0xE0    ; $160B  MAPLO high (X)
meganet_tr_map_y: .byte 0x00    ; $160C  MAPHI low  (Y)
meganet_tr_map_z: .byte 0x83    ; $160D  MAPHI high (Z): $E000 -> $3E000

meganet_tr_call:                ; $1617
        php
        sei

        ; Megabyte registers to zero for both halves first, whatever the
        ; caller left latched: a plain MAP keeps the last megabyte, and
        ; BASIC 65's SYS path leaves it non-zero, which put bank 4 in
        ; unmapped space and ended every call in a warm start
        ; (REQUIREMENTS.md 5.10). X/Z = $0F is the megabyte form.
        lda     #0x00
        ldx     #0x0f
        ldy     #0x00
        ldz     #0x0f
        map
        ; Bank 4 over $2000-$BFFF -- and over $E000-$FFFF, so that the
        ; interrupt vectors the CPU would fetch during the call are the
        ; stack's own ($4FFFA-$4FFFF, an RTI stub) rather than whatever
        ; the caller keeps in bank 0 under its KERNAL, which under BASIC 65
        ; is variable space that reads as BRK (REQUIREMENTS.md 5.11).
        lda     #0x00
        ldx     #0xe4
        ldy     #0x00
        ldz     #0xb4
        map
        eom

        lda     meganet_tr_addr
        sta     jsr_target + 1
        lda     meganet_tr_addr + 1
        sta     jsr_target + 2

        lda     meganet_tr_a
        ldx     meganet_tr_x
        ldy     meganet_tr_y
        ldz     meganet_tr_z
jsr_target:
        jsr     0x0000          ; operand patched above

        sta     meganet_tr_ra
        stx     meganet_tr_rx
        sty     meganet_tr_ry
        stz     meganet_tr_rz

        ; The caller's real map back -- KERNAL included.
        lda     meganet_tr_map_a
        ldx     meganet_tr_map_x
        ldy     meganet_tr_map_y
        ldz     meganet_tr_map_z
        map
        eom
        ldz     #0x00           ; C is about to run: STZ must store zero
        plp
        rts
