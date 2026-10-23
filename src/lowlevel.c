#include <stddef.h>
#include <stdint.h>
#include "include/lowlevel.h"

const char * cpuid_processor_family_ids_amd[CPUID_PROCESSOR_LIST_LEN] = {
    [4] = "AMD 486",
    [5] = "K5/K6",
    [6] = "Athlon",
    [0xF] = "Athlon 64",
    [0x10] = "Phenom",
    [0x11] = "Turion X2",
    [0x12] = "Llano",
    [0x14] = "Bobcat",
    [0x15] = "Steamroller (K15)",
    [0x16] = "Jaguar/Puma",
    [0x17] = "Zen 1/2",
    [0x18] = "Hygon Dhyana",
    [0x19] = "Zen 3/4",
    [0x20] = "Zen 5/6"
};
const char * cpuid_processor_family_ids_intel[CPUID_PROCESSOR_LIST_LEN] = {
    [0x4] = "Intel 486",
    [0x5] = "Pentium",
    [0x6] = "Pentium II+/Core/Atom/Xeon",
    [0x7] = "Itanium (IA-32)",
    [0xB] = "Xeon Phi",
    [0xF] = "NetBurst (Pentium 4)",
    [0x11] = "Itanium 2 (IA-32)",
    [0x12] = "Intel Core",
    [0x13] = "Xeon"
};

void outb(uint16_t port, uint8_t data) {
    asm volatile (
        "out %b0, %w1"
        :
        : "a" (data), "Nd" (port)
    );
}
void outw(uint16_t port, uint16_t data) {
    asm volatile (
        "outw %0, %1"
        :
        : "ax" (data), "Nd" (port)
    );
}
void outl(uint16_t port, uint32_t data) {
    asm volatile (
        "out %0, %1"
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
uint16_t inw(uint16_t port) {
    uint16_t data;
    asm volatile (
        "inw %1, %0"
        : "=ax" (data)
        : "Nd" (port)
    );
    return data;
}
uint32_t inl(uint16_t port) {
    uint32_t data;
    asm volatile (
        "inl %1, %0"
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

// for some fucking reason __builtin_ffs works on SIGNED INTEGERS
// returns 1 + the index, or 0
// can't use no_caller_saved_registers here, because gcc can't detect x87 usage and refused to compile
__attribute__((naked, pure, regparm(1)/*, no_caller_saved_registers*/)) unsigned int bsf(unsigned int x) {
    asm volatile (
        "bsfl %eax, %eax;"
        "jz 1f;"
        "incl %eax;"
        "ret\n"
        "1:\n"
        "xorl %eax, %eax;" // bsfl leaves eax unchanged on x being 0
        "ret"
    );
}
