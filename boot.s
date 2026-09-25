.set ALIGN,        1<<0
.set MEMINFO,      1<<1
.set AOUT_KLUDGE,  1<<16          
.set FLAGS,        ALIGN | MEMINFO | AOUT_KLUDGE
.set MAGIC,        0x1BADB002
.set CHECKSUM,     -(MAGIC + FLAGS)

.section .multiboot
.align 4
multiboot_header:
    .long MAGIC
    .long FLAGS
    .long CHECKSUM
    .long multiboot_header      
    .long 0x100000               
    .long 0                  
    .long 0                       
    .long _start                  

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
    
    
