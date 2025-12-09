#include <stddef.h>
#include <stdint.h>
#include "include/lowlevel.h"


void outb(uint16_t port, uint8_t data) {
    asm volatile (
        "out %b0, %w1"
        :
        : "a" (data), "Nd" (port)
    );
}
uint8_t inb(uint16_t port) {
    uint8_t data;
    asm volatile (
        "in %w1, %b0"
        : "=a" (data)
        : "Nd" (port)
    );
    return data;
}


#define IO_WAIT_UNUSED_PORT 0x80
void io_wait() { // using the fact that io port operations are not instant
    outb(IO_WAIT_UNUSED_PORT, 0);
}

char is_cpuid_supported() {
    uint32_t is_supported = 0;
    asm volatile (
        "pushf\n\t"
        "orl $"STR(IA_32_EFL_CPUID)", (%%esp)\n\t" //if we start of with ID flag 0
        "popf\n\t"
        "pushf\n\t" // once to restore, once to edit
        "pushf\n\t"
        "xorl $" STR(IA_32_EFL_CPUID)", (%%esp)\n\t"
        "popf; pushf\n\t" // save the edit, get the new flags
        "pop %%eax\n\t"
        "xorl (%%esp), %%eax\n\t" // if not zero, then we can change the value
        "popf\n\t" //restore previous flags
        "andl $" STR(IA_32_EFL_CPUID) ", %%eax\n\t"
        : "=a"(is_supported) 
    );
    return is_supported!=0;
}