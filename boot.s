.set ALIGN,        1<<0
.set MEMINFO,      1<<1
.set AOUT_KLUDGE,  1<<16          /* Bật cờ AOUT Kludge để QEMU load flat binary */
.set FLAGS,        ALIGN | MEMINFO | AOUT_KLUDGE
.set MAGIC,        0x1BADB002
.set CHECKSUM,     -(MAGIC + FLAGS)

.section .multiboot
.align 4
multiboot_header:
    .long MAGIC
    .long FLAGS
    .long CHECKSUM
    /* Các trường AOUT Kludge bắt buộc: */
    .long multiboot_header        /* Header addr */
    .long 0x100000                /* Load addr */
    .long 0                       /* Load end addr (0 = load toàn bộ file) */
    .long 0                       /* BSS end addr */
    .long _start                  /* Entry point */

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .text
.global _start
.global start
_start:
start:
    mov $stack_top, %esp
    and $-16, %esp
    cli
    call kernel_main

.loop:
    cli
    hlt
    jmp .loop
    