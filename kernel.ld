OUTPUT_FORMAT(elf32-i386)
ENTRY(kernel_entry)
SECTIONS
{
    . = 0x10000;
    .text : { *(.text.boot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss ALIGN(16) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }
}
