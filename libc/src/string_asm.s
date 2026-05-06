.section .text
.global memset
.type memset, @function
/* void * memset(void * s, char c, size_t n) */
memset:
    cld
    push %ebp
    mov  %esp, %ebp
    push %edi
    push %esi

    mov 0x8(%ebp),  %edi /* s */
    mov 0xC(%ebp),  %eax /* c */
    mov 0x10(%ebp), %ecx /* n */

    cmp $0x8, %ecx
    jg 0f
    rep stosb
    pop %esi
    pop %edi
    pop %ebp
    ret

    0:
    /* set unaligned first bytes */
    mov %edi, %edx
    and $0x3, %edx
    jz 1f

    mov $0x4, %esi
    sub %edx, %esi
    sub %esi, %ecx

    2:
        mov %al, (%edi, %esi, 1)
        dec %esi
        jnz 2b


    /* main memset */
    1:
    mov %ecx, %esi
    and $0x3, %esi
    shr $0x2, %ecx;
    rep stosl
    and $0x3, %esi
    jz 4f

    /* set unaligned last bytes */
    3:
        mov %al, (%edi, %esi, 1)
        dec %esi
        jnz 3b

    4:
    pop %esi
    pop %edi
    pop %ebp
    mov (%ebp), %eax
    ret


.section .text
.global memcpy
.type memcpy, @function
/* void * memcpy(void *__restrict dest, const void *__restrict src, size_t n) */
memcpy:
    push %ebp
    mov %esp, %ebp
    push %edi
    push %esi

    mov 0x08(%ebp), %edi /* dest */
    mov 0x0C(%ebp), %esi /* src */
    mov 0x10(%ebp), %ecx /* n */

    rep movsb

    pop %esi
    pop %edi
    pop %ebp
    ret