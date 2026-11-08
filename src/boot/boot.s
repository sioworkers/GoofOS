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

    /* read the sector-count header from ds1 to 0x8000. */
    movb boot_drive, %dl
    movw $disk_packet, %si
    movw $1, disk_packet+2
    movw $0, disk_packet+4
    movw $0x0800, disk_packet+6
    movl $1, disk_packet+8
    movl $0, disk_packet+12
    movb $0x42, %ah
    int $0x13
    jc disk_error

    /* header is a 16-bit sector count, reject empty/oversized kernels. */
    movw 0x8000, %ax
    testw %ax, %ax
    jz disk_error
    cmpw $127, %ax
    ja disk_error

    /* load that many sectors from ds2 to 0x10000. */
    movb boot_drive, %dl
    movw $disk_packet, %si
    movw %ax, disk_packet+2
    movw $0, disk_packet+4
    movw $0x1000, disk_packet+6
    movl $2, disk_packet+8
    movl $0, disk_packet+12
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
    .word 1
    .word 0
    .word 0x0800
    .quad 1

boot_drive:
    .byte 0
boot_message:
    .asciz "Hello from Boot!\r\n" # idk
error_message:
    .asciz "Disk read failed.\r\n" # we're cooked

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
