#ifndef _SETJMP_H
#define _SETJMP_H

typedef unsigned long jmp_buf[9];
/*
struct {
    unsigned long ebx;
    unsigned long esi, edi;
    unsigned long ebp, esp;
    unsigned long rip;
    unsigned long sigs_saved;
    sigset_t sigs; // 64 bits
} typedef jmp_buf;
*/
typedef jmp_buf sigjmp_buf;

int setjmp(jmp_buf env);
int sigsetjmp(sigjmp_buf env, int savemask);
__attribute__((noreturn)) void longjmp(jmp_buf env, int val);
__attribute__((noreturn)) void siglongjmp(sigjmp_buf env, int val);
#endif