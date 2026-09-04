#include "lib2.h"
#include "lib3.h"

static __attribute__((naked)) void print(char * s, unsigned long n) {
    asm volatile (
        "pushl %esi\n"
        "pushl %edi\n"
        "movl $0xE, %eax\n"
        "movl $2, %edi\n"
        "movl 0x8(%esp), %esi\n"
        "movl 0xC(%esp), %edx\n"
        "int $0xF0\n"
        "popl %edi\n"
        "popl %esi\n"
        "ret\n"
    );
}

void lib1() {
    print("lib1\n", 5);
    static char ran = 0;
    if (ran) return;
    ran = 1;
    lib2();
    lib3();
}