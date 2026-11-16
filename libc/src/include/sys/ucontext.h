#ifndef _SYS_UCONTEXT_H
#define _SYS_UCONTEXT_H

#include <signal.h>

#define NGREG 13
typedef int greg_t;
typedef greg_t gregset_t[NGREG];

#define REG_EDI  0
#define REG_ESI  1
#define REG_EBP  2
#define REG_ESP  3
#define REG_EBX  4
#define REG_EDX  5
#define REG_ECX  6
#define REG_EAX  7
#define REG_EIP  8
#define REG_CS   9
#define REG_EFL  10
#define REG_UESP 11
#define REG_SS   12

struct interr_frame {
    void * ip; // instruction pointer
    unsigned long cs; // code segment
    unsigned long flags; // check flags, e.g. LT IZ ...
    void * sp; // stack pointer
    unsigned long ss; // stack segment
} __attribute__((packed));

struct {
    unsigned long edi, esi;
    void *ebp, *esp;
    unsigned long ebx, edx, ecx, eax;

    struct interr_frame iret_frame;
}  __attribute__((packed)) typedef __gregcontext_t;

struct {
    union {
        struct {
            union {
                struct {
                    int state[27];
                    int status;
                } fpchip_state;

                struct {
                    char fp_emul[246];
                    char fp_epad[2];
                } fp_emul_space;

                int f_fpregs[62];
            } fp_reg_set;

            long int f_wregs[33];
        };
        unsigned char __[512];
    };
} __attribute__((packed)) typedef fpregset_t;

struct {
    union {
        gregset_t gregs;
        __gregcontext_t __gregs;
    };
    __attribute__((aligned(16))) fpregset_t fpregs;
} typedef mcontext_t;

struct ucontext_t {
    struct ucontext_t * uc_link;
    sigset_t uc_sigmask;
    stack_t uc_stack;
    mcontext_t uc_mcontext;
} typedef ucontext_t;

#endif