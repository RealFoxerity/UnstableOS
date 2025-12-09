.section .init
    # gcc will add crtend.o .init here
    popl %ebp
    ret

.section .fini
    # gcc will add crtend.o .fini here
    popl %ebp
    ret
    