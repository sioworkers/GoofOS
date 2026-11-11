/* bakes the compiled programs/main.gof into the kernel image. */
.section .rodata
.global program_start
.global program_end

program_start:
    .incbin "main.gex"
program_end:
