#ifndef LOWLEVEL_H
#define LOWLEVEL_H
#include <stdint.h>

#define __STR_INNER(x) #x
#define STR(x) __STR_INNER(x)

#define IA_32_EFL_STATUS_CARRY 1 // 1 = has carry
#define IA_32_EFL_ALWAYS_1 2
#define IA_32_EFL_STATUS_PARITY 4 // 1 = parity even
#define IA_32_EFL_STATUS_ADJUST 0x10 // "Set if arithmetic operation genereates a carry or a borrow out of bit 3 of the result", BCD, 1 = has aux carry
#define IA_32_EFL_STATUS_ZERO 0x40 // 1 = 0
#define IA_32_EFL_STATUS_SIGN 0x80 // 1 = negative
#define IA_32_EFL_SYSTEM_TRAP 0x100 // control register, set = single step mode
#define IA_32_EFL_SYSTEM_INTER_EN 0x200 // set to 1 = enable interrupts
#define IA_32_EFL_DIRECTION 0x400 // controls string operations (MOVS, CMPS, SCAS, LODS, STOS) 1 = autodecrement
#define IA_32_EFL_STATUS_OVERFLOW 0x800 // 1 = overflowed
#define IA_32_EFL_SYSTEM_IO_PRIV 0x3000 // >> 12 = ring level of current task
#define IA_32_EFL_SYSTEM_NESTED_TASK 0x4000 // set to 1 = the current task is linked to previous
#define IA_32_EFL_SYSTEM_RESUME 0x10000 // "Controls the processor's response to debug exceptions." Intel IA-32 Software Developer's Manual page 3-23
#define IA_32_EFL_SYSTEM_VM8086 0x20000 // set to 1 = enter virtual 8086, clear to return to protected mode
#define IA_32_EFL_SYSTEM_ALIGN_CHECK 0x40000 // set to 1 = enable memory reference alignment checking (AM bit in CR0)
#define IA_32_EFL_VIRT_INTER 0x80000 // "Virtual image of the IF (inter_en) flag. Used in conjuction with the VIP flag."..."enabled by setting VME flag in CR4"
#define IA_32_EFL_VIRT_INTER_PEND 0x100000 // set to 1 = interrupt is pending, cpu only reads (software must set), Used in conjunction with VIF
#define IA_32_EFL_CPUID 0x200000 // can be changed = CPUID instruction is available


#define CPUID_1_GET_FAMILY(x) ((x>>8)&0xFF)
#define CPUID_1_GET_EXT_FAMILY(x) ((x>>20)&0xFFFF)
#define CPUID_1_GET_MODEL(x) ((x>>4)&0x0F)
#define CPUID_1_GET_EXT_MODEL(x) ((x>>16)&0x0F)
#define CPUID_1_GET_TYPE(x) ((x>>12)&3)
#define CPUID_1_GET_STEPPING(x) (x&0x0F)
#define CPUID_1_GET_ADD_LOGICAL_PROC(x) ((x>>16) & 0xFF)


#define CPUID_1_FFLAGS_D_GET_FPU(x) (x&1)
#define CPUID_1_FFLAGS_D_GET_VME(x) ((x>>1)&1) // vm8086 extensions
#define CPUID_1_FFLAGS_D_GET_DE(x) ((x>>2)&1) // debugging extensions
#define CPUID_1_FFLAGS_D_GET_PSE(x) ((x>>3)&1) // supports 4M pages
#define CPUID_1_FFLAGS_D_GET_TSC(x) ((x>>4)&1) // time stamp counter and RDTSC
#define CPUID_1_FFLAGS_D_GET_MSR(x) ((x>>5)&1) // RDMSR and WRMSR
#define CPUID_1_FFLAGS_D_GET_PAE(x) ((x>>6)&1)
#define CPUID_1_FFLAGS_D_GET_MCE(x) ((x>>7)&1) // machine check exception
#define CPUID_1_FFLAGS_D_GET_CX8(x) ((x>>8)&1) // CMPXCHG8B
#define CPUID_1_FFLAGS_D_GET_APIC(x) ((x>>9)&1) // contains onboard apic
#define CPUID_1_FFLAGS_D_GET_SEP(x) ((x>>11)&1) // has sysenter/sysexit
#define CPUID_1_FFLAGS_D_GET_ACPI(x) ((x>>22)&1) // Onboard thermal control MSRs for ACPI 
#define CPUID_1_FFLAGS_D_GET_HTT(x) ((x>>28)&1) // "Max APIC IDs reserved field is Valid" (hyperthreading is enabled)


#define CPUID_1_FFLAGS_C_GET_VMX(x) ((x>>5)&1)
#define CPUID_1_FFLAGS_C_GET_TM2(x) ((x>>8)&1) // supports thermal monitor 2
#define CPUID_1_FFLAGS_C_GET_X2APIC(x) ((x>>21)&1)
#define CPUID_1_FFLAGS_C_GET_HYPERVISOR(x) ((x>>31)&1) // hypervisor present

enum cpuid_funcs {
    CPUID_VENDOR_ID = 0,
    CPUID_PROCESSOR_INFO_FEATURES = 1,
    CPUID_SERIAL_NUMBER = 3,
    CPUID_THERMAL_POWER = 6,
};

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
void io_wait();

char is_cpuid_supported();
#endif