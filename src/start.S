@ Entry point, called by the boot ROM / NAND bootloader through an sb "call"
@ after play0 has initialised the LCD. We run on our own stack (the frame
@ buffers are large). main() returning 0 hands control back to the caller,
@ so the original player (play1) loads next; it normally never returns.
    .section .text.start, "ax"
    .arm
    .global _start
_start:
    stmfd   sp!, {r4-r12, lr}
    ldr     r1, =caller_sp
    str     sp, [r1]
    ldr     sp, =stack_top
    bl      main
    ldr     r1, =caller_sp
    ldr     sp, [r1]
    ldmfd   sp!, {r4-r12, lr}
    bx      lr

    .bss
    .align  3
caller_sp:
    .space  4
    .align  3
    .space  4096
stack_top:
