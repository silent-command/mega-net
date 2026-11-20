/* Copyright 2026 Justin Cameron
 * SPDX-License-Identifier: Apache-2.0 */
; The RFC 1071 sum on the 45GS02, for mn_checksum.c's sum16 on the
; target: the C, compiled, spent about 40 instructions a 16-bit word;
; this spends 13. It is every byte the stack sends or receives, twice:
; once into the checksum it writes, once checking the one it was sent.
;
; In: mn_cs_ptr (the data, a CPU address) and mn_cs_len (bytes); out:
; mn_cs_sum, the ones-complement sum of the big-endian words, the last
; odd byte padded low, not complemented. The words are added high byte
; to high, low to low, with the carry out of each word carried into the
; next one: deferring the end-around carry like that is allowed in
; ones-complement arithmetic (RFC 1071 2.(B)), and it lets the whole
; loop keep the carry in C, since nothing in it but adc touches C
; (iny, dex, inc, lda, sta). The last carry goes round at the end.
;
; The pointer is walked through __rc2/__rc3, the compiler's first
; pointer register: caller-saved, so free in a leaf (ABI). Z is never
; touched; every branch is short.

        .section .bss.mn_cs,"aw",@nobits
        .global mn_cs_ptr, mn_cs_len, mn_cs_sum
mn_cs_ptr:  .skip 2
mn_cs_len:  .skip 2
mn_cs_sum:  .skip 2                 ; high byte first in the sum, but stored as a uint16 (low byte first)
cs_hi:      .skip 1                 ; words left, in 256s
cs_tmp:     .skip 1

        .text
        .global mn_sum16_m65
mn_sum16_m65:
        lda mn_cs_ptr
        sta __rc2
        lda mn_cs_ptr+1
        sta __rc3
        lda #0
        sta mn_cs_sum
        sta mn_cs_sum+1
        lda mn_cs_len+1             ; words = len / 2, as cs_hi:X
        lsr
        sta cs_hi
        lda mn_cs_len
        ror
        tax
        ldy #0
        clc
        txa                         ; (lda and tax leave C alone)
        beq cs_more
cs_word:
        lda (__rc2),y               ; the high byte
        sta cs_tmp
        iny
        lda (__rc2),y               ; the low byte
        adc mn_cs_sum
        sta mn_cs_sum
        lda cs_tmp
        adc mn_cs_sum+1
        sta mn_cs_sum+1
        iny                         ; words start at even offsets, so Y wraps between words only
        bne cs_same_page
        inc __rc3
cs_same_page:
        dex
        bne cs_word
cs_more:
        lda cs_hi                   ; another 256 words?
        beq cs_tail
        dec cs_hi
        ldx #0                      ; 256 more: dex from 0 wraps to 255
        jmp cs_word
cs_tail:
        lda mn_cs_len               ; an odd byte: the high half of a last word, padded low
        and #1
        beq cs_fold
        lda (__rc2),y
        sta cs_tmp
        lda mn_cs_sum               ; (and leaves C alone; lda too)
        adc #0
        sta mn_cs_sum
        lda cs_tmp
        adc mn_cs_sum+1
        sta mn_cs_sum+1
cs_fold:
        lda mn_cs_sum               ; the carry still in C goes round; at most twice
        adc #0
        sta mn_cs_sum
        lda mn_cs_sum+1
        adc #0
        sta mn_cs_sum+1
        lda mn_cs_sum
        adc #0
        sta mn_cs_sum
        lda mn_cs_sum+1
        adc #0
        sta mn_cs_sum+1
        rts
