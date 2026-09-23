; The 45GS02 STZ hazard, in twelve bytes. llvm-mos compiles "store zero"
; as STZ, which on this CPU stores the Z register. Load Z, then let C
; store a zero, and the zero is not a zero.
        .section .text,"ax",@progbits
        .global stz_set_z, stz_clear_z
stz_set_z:      ldz     #0x83
                rts
stz_clear_z:    ldz     #0x00
                rts
