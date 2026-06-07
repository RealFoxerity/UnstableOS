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

    movl 0x8(%ebp),  %edi /* s */
    movl 0xC(%ebp),  %eax /* c */
    movl 0x10(%ebp), %ecx /* n */

    cmp $0x8, %ecx
    jg 0f
    rep stosb
    jmp 4f

    0:
    /* set unaligned first bytes */
    mov %edi, %edx
    and $0x3, %edx
    jz 1f

    mov $0x4, %esi
    sub %edx, %esi
    sub %esi, %ecx
    pushl %ecx
    mov %esi, %ecx
    rep stosb
    pop %ecx


    /* main memset */
    1:

    /* distribute the c byte into all positions so that stosl works */
    movl %eax, %esi
    shl $0x8, %esi
    orl %esi, %eax
    movl %eax, %esi
    shl $0x10, %eax
    orl %esi, %eax

    mov %ecx, %esi
    shr $0x2, %ecx;
    rep stosl
    and $0x3, %esi
    jz 4f

    /* set unaligned last bytes */
    mov %esi, %ecx
    rep stosb

    4:
    pop %esi
    pop %edi
    mov 0x8(%ebp), %eax
    pop %ebp
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