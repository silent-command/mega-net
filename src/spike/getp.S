; uint8_t mn_get_p(void): the processor status register, so C can see
; whether interrupts are enabled (bit 2 clear) after an ABI call.
        .section .text,"ax",@progbits
        .global mn_get_p
mn_get_p:
        php
        pla
        ldx     #0
        rts
