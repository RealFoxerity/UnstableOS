#include <setjmp.h>

__attribute__((naked)) int setjmp(jmp_buf env) {
    asm volatile (
        "movl 4(%esp), %eax;"
        "movl %ebx, 0x00(%eax);"
        "movl %esi, 0x04(%eax);"
        "movl %edi, 0x08(%eax);"
        "movl %ebp, 0x0C(%eax);"
        "leal 0x4(%esp), %ecx;"
        "movl %ecx, 0x10(%eax);" // have to get rid of the rip
        "movl (%esp), %ecx;"
        "movl %ecx, 0x14(%eax);" // rip
        "xorl %ecx, %ecx;"
        "xorl %ecx, 0x18(%eax);" // sigs_saved
        "ret"
    );
}


#include <signal.h>
#include <stddef.h>
#include <stdio.h>
__attribute__((used)) static int __sigsetjmp(sigjmp_buf env, int savemask) {
    env[6] = savemask;
    if (savemask)
        pthread_sigmask(0, NULL, (sigset_t*)(env + 7));
    return 0;
}

__attribute__((naked)) int sigsetjmp(jmp_buf env, int savemask) {
    asm volatile (
        "movl 4(%esp), %eax;"
        "movl %ebx, 0x00(%eax);"
        "movl %esi, 0x04(%eax);"
        "movl %edi, 0x08(%eax);"
        "movl %ebp, 0x0C(%eax);"
        "leal 0x4(%esp), %ecx;"
        "movl %ecx, 0x10(%eax);" // have to get rid of the rip
        "movl (%esp), %ecx;"
        "movl %ecx, 0x14(%eax);" // rip
        "jmp __sigsetjmp;"
    );
}

__attribute__((naked,noreturn)) void longjmp(jmp_buf env, int val) {
    asm volatile (
        "movl 0x08(%esp), %eax;"
        "cmpl $1, %eax;" // basically does eax - 1, which if eax is 0 sets CF
        "adc $0, %al;" // adds 0 + CF to al, al because eax would be 3 bytes longer
        "movl 0x04(%esp), %ecx;"

        "movl 0x00(%ecx), %ebx;"
        "movl 0x04(%ecx), %esi;"
        "movl 0x08(%ecx), %edi;"
        "movl 0x0C(%ecx), %ebp;"
        "movl 0x10(%ecx), %esp;"
        "jmp *0x14(%ecx);"
    );
}


__attribute__((noreturn)) void siglongjmp(sigjmp_buf env, int val) {
    if (env[6])
        pthread_sigmask(SIG_SETMASK, (sigset_t*)(env + 7), NULL);
    longjmp(env, val);
    __builtin_unreachable();
}
