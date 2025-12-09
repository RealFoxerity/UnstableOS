.section .init
.global _init
_init:
    # set up stack frame
    pushl %ebp
    movl %esp, %ebp
    # gcc will add crtbegin.o .init here


.section .fini
.global _fini
_fini:
    # set up stack frame
    pushl %ebp
    movl %esp, %ebp
    # gcc will add crtbegin.o .fini here
