; IRQ witness: the KERNAL's IRQ entry chains through ($0314). Point that
; at us, count, and continue into the original handler. Runs after the
; KERNAL entry has saved the registers; INC only touches flags.
        .section .noinit,"aw",@nobits
        .global mn_irq_orig
mn_irq_orig: .skip 2
        .section .text,"ax",@progbits
        .global mn_irq_hook
mn_irq_hook:
        inc     0x1490
        jmp     (mn_irq_orig)
