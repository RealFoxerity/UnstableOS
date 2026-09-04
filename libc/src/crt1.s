.section .text
.global _start
.type _start, @function

_start:
    # end of stack frame
    xorl %ebp, %ebp
    pushl %ebp # eip
    pushl %ebp # ebp

    movl %esp, %ebp

    pushl %ebp      # argv
    addl $0xC, (%esp)

    pushl 0x8(%ebp) # argc
    pushl $0 # rtld fini
    call __libc_init
