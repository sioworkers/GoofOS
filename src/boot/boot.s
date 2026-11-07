.section .boot, "ax"
.code16
.global boot_start

boot_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    sti

    /* 80x25 text mode */
    movw $0x0003, %ax
    int $0x10

    movb %dl, boot_drive
    movw $boot_message, %si
    call print_string

    /* load 32 sectors after this boot sector to 0x10000. */
    movb boot_drive, %dl
    movw $disk_packet, %si
    movb $0x42, %ah
    int $0x13
    jc disk_error

    cli
    lgdt gdt_descriptor
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $protected_mode

print_string:
    lodsb
    testb %al, %al
    jz print_done
    movb $0x0e, %ah
    movb $0x00, %bh
    int $0x10
    jmp print_string
print_done:
    ret

disk_error:
    movw $error_message, %si
    call print_string
halt16:
    cli
    hlt
    jmp halt16

.align 4
disk_packet:
    .byte 0x10
    .byte 0
    .word 32
    .word 0
    .word 0x1000
    .quad 1

boot_drive:
    .byte 0
boot_message:
    .asciz "Hello from Boot!\r\n" ; idk
error_message:
    .asciz "Disk read failed.\r\n" ; we're cooked

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

.code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp
    movl $0x10000, %eax
    call *%eax
halt32:
    cli
    hlt
    jmp halt32

.org 510
.word 0xaa55
