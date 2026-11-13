/* entry points for the 32 cpu exceptions. every stub leaves the stack
   looking the same: vector and error code on top of what the cpu pushed,
   so idt.cpp only needs one handler. */

.section .text

/* exceptions where the cpu doesn't push an error code: push a fake 0. */
.macro isr_no_error vector
isr\vector:
    pushl $0
    pushl $\vector
    jmp isr_common
.endm

/* exceptions where the cpu already pushed an error code. */
.macro isr_error vector
isr\vector:
    pushl $\vector
    jmp isr_common
.endm

isr_no_error 0   /* division error */
isr_no_error 1   /* debug */
isr_no_error 2   /* non-maskable interrupt */
isr_no_error 3   /* breakpoint */
isr_no_error 4   /* overflow */
isr_no_error 5   /* bound range exceeded */
isr_no_error 6   /* invalid opcode */
isr_no_error 7   /* device not available */
isr_error    8   /* double fault */
isr_no_error 9   /* coprocessor segment overrun */
isr_error    10  /* invalid tss */
isr_error    11  /* segment not present */
isr_error    12  /* stack-segment fault */
isr_error    13  /* general protection fault */
isr_error    14  /* page fault */
isr_no_error 15  /* reserved */
isr_no_error 16  /* x87 floating-point exception */
isr_error    17  /* alignment check */
isr_no_error 18  /* machine check */
isr_no_error 19  /* simd floating-point exception */
isr_no_error 20  /* virtualization exception */
isr_error    21  /* control protection exception */
isr_no_error 22
isr_no_error 23
isr_no_error 24
isr_no_error 25
isr_no_error 26
isr_no_error 27
isr_no_error 28
isr_no_error 29
isr_error    30  /* security exception */
isr_no_error 31

/* save every register, hand a pointer to all of it to idt.cpp, and come
   back out the same way if the handler ever returns. */
isr_common:
    pushal
    cld                     /* c code expects the direction flag clear */
    pushl %esp              /* argument: pointer to the saved frame */
    call exception_handler
    addl $4, %esp
    popal
    addl $8, %esp           /* drop vector and error code */
    iret

/* hardware interrupts, remapped by irq.cpp to vectors 32-47. same stack
   layout as the exceptions, with 0 as the error code. */
.macro irq_stub line
irq\line:
    pushl $0
    pushl $(32 + \line)
    jmp irq_common
.endm

.irp line, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    irq_stub \line
.endr

/* unlike exceptions, irqs always come back: the interrupted code carries on
   right where it was. */
irq_common:
    pushal
    cld
    pushl %esp
    call irq_handler
    addl $4, %esp
    popal
    addl $8, %esp
    iret

/* addresses of the stubs, in vector order, for idt.cpp and irq.cpp. */
.section .rodata
.global isr_stubs
isr_stubs:
.irp vector, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .long isr\vector
.endr

.global irq_stubs
irq_stubs:
.irp line, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    .long irq\line
.endr
