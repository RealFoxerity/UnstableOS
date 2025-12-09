/* https://www.gnu.org/software/grub/manual/multiboot/multiboot.html */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO  /* this is the Multiboot 'flag' field */
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM


/* creating alligned stack */
/* stack grows downwards on x86 */

.section .bss
.align 16
_kernel_stack_bottom:
    .skip 1<<12 /* 4 kib of stack, 1 page */
.global _kernel_stack_top
_kernel_stack_top:

.section .text
.global _start
.type _start, @function

errmsg: .string "KERNEL THREAD TERMINATED"

_start:
    mov $_kernel_stack_top, %esp
    push %eax
    push %ebx
    cli /* kernel will enable interrupts itself, need to disable so that nothing interrupts setup */
    call kernel_entry
    
    /* kernel_entry returned, should not happen */
    cli
    push $errmsg
    call panic
1:  hlt
    jmp 1b /* infinite loop*/
