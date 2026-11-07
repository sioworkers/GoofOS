OUTPUT_FORMAT(elf32-i386)
ENTRY(boot_start)
SECTIONS
{
    . = 0x7c00;
    .boot : { *(.boot) }
    /DISCARD/ : { *(*) }
}
