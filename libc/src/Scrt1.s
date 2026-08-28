.section .text

.type __stack_chk_fail_local, %function
.weak __stack_chk_fail_local
__stack_chk_fail_local:
	call __stack_chk_fail@plt

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

    addl $_GLOBAL_OFFSET_TABLE_+[.-1b], %ebx
    push main@GOT(%ebx)

    call __libc_init@plt
