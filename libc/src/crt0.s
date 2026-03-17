.section .text

.global _start
_start:
    # end of stack frame
    movl $0, %ebp
    pushl %ebp # eip
    pushl %ebp # ebp

    movl %esp, %ebp

    pushl %ebp      # argv
    addl $0xC, (%esp)

    pushl 0x8(%ebp) # argc

    call __libc_init
    call _init

    call main
    movl %eax, %edi
    movl $0, %eax # exit syscall number
    int $0xF0 # syscall interrupt
.size _start, . - _start
