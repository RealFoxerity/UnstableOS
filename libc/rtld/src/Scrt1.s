.section .text
.global _start
.type _start, @function

_start:
    xorl %ebp, %ebp
    pushl %ebp # eip
    pushl %ebp # ebp

    movl %esp, %ebp
    call 1f
1:
    popl %ebx

    pushl %ebp      # argv
    addl $0xC, (%esp)

    pushl 0x8(%ebp) # argc

    call __rtld_main
    addl $0x10, %esp
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    movl $call_fini, %edx

    xorl %esi, %esi
    xorl %edi, %edi

    xorl %ebp, %ebp

    pushl %eax # the _start of the target executable for ret
    xorl %eax, %eax

    fninit
    fnclex
    pushl $0x202
    popf
    ret