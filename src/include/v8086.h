#ifndef V8086_H
#define V8086_H
#include <stddef.h>
struct v86_interr_frame {
    void * ip;
    unsigned long cs;
    unsigned long flags;
    void * sp;
    unsigned long ss, es, ds, fs, gs;
} __attribute__((packed));

struct {
    unsigned long edi, esi;
    void * ebp, * esp;
    unsigned long ebx, edx, ecx, eax;

    struct v86_interr_frame iret_frame;
} __attribute__((packed)) typedef v86_mcontext_t;

v86_mcontext_t run_v86_task(void * code_start, size_t code_size,  void * final_ip, v86_mcontext_t regs);
v86_mcontext_t v86_call_bios(unsigned char irq, v86_mcontext_t regs);
void v86_monitor(unsigned long error, v86_mcontext_t * ctx);

#endif