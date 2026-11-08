.section .text.boot, "ax"
.code32
.global kernel_entry
.type kernel_entry, @function

/* boot.s enters here at 0x10000 with flat segments and interrupts disabled. */
kernel_entry:
    cld

    /* clear bss while the bootloader's temporary stack is still active. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    shrl $2, %ecx
    xorl %eax, %eax
    rep stosl

    /* use the kernel's own stack from this point on. */
    movl $kernel_stack_top, %esp
    andl $-16, %esp
    call kernel_main

halt:
    cli
    hlt
    jmp halt

.section .bss
.align 16
kernel_stack_bottom:
    .skip 16384
kernel_stack_top:
