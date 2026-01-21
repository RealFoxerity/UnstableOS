#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#include "include/kernel_interrupts.h"
#include "include/kernel_tty.h"
#include "include/lowlevel.h"
#include "include/multiboot.h"
#include "../libc/src/include/string.h"
#include "include/kernel.h"
#include "include/mm/kernel_memory.h"
#include "include/kernel_gdt_idt.h"
#include "include/ps2_keyboard.h"
#include "include/elf.h"
#include "include/rs232.h"
#include "include/kernel_sched.h"
#include "include/timer.h"
#include "../libc/src/include/stdlib.h"
#include "include/kernel_tty_io.h"
#include "include/vga.h"

// clang is insanely annoying, used because uint32_t is smaller than native pointer size (64 bit int) on my machine
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"

#define tty_write(buf, count) tty_write(buf, count); com_write(1, buf, count);

void panic(char * reason) { // using vga_write and com_write in case we don't have tty at that point and/or it would cause a recursive panic/deadlock
    char errmsg[128];
    sprintf(errmsg, "\n\n##############################\nKernel Panic: %s", reason);
    vga_write(errmsg, strlen(errmsg));
    com_write(0, errmsg, strlen(errmsg));
    asm volatile (
        "cli\n\t"
        "hlt\n\t"
    );
    __builtin_unreachable();
}


struct interrupt_gate_descriptor {
    uint16_t offset;
    uint16_t segment_selector;
    uint8_t interrupt_stack_table_offset;
    uint8_t flags_gt_dpl_p;
    uint16_t offset2;
    uint32_t offset3;
    uint32_t reserved;
} __attribute__((packed))__;

void kernel_entry_addr_log() {
    kprintf("Kernel loaded at physical address from 0x%x to 0x%x\n", &_kernel_base, &_kernel_top);
}

#define CPUID_PROCESSOR_LIST_LEN 0x21
#define CPUID_MANUFACTURER_AMD "AuthenticAMD"
#define CPUID_MANUFACTURER_INTEL "GenuineIntel"
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
    [0x6] = "Pentium II+/Intel Core/Intel Atom/Xeon",
    [0x7] = "Itanium (IA-32)",
    [0xB] = "Xeon Phi",
    [0xF] = "NetBurst (Pentium 4)",
    [0x11] = "Itanium 2 (IA-32)",
    [0x12] = "Intel Core",
    [0x13] = "Xeon"
};


void kernel_print_cpu_info() {
    if (!is_cpuid_supported()) {
        kprintf("Kernel: CPUID instruction is not supported\n");
        return;
    }

    // get vendor id
    unsigned long vendor1, vendor2, vendor3, largest_supported, largest_supported_ext;
    asm volatile (
        "cpuid\n\t"
        :"=b" (vendor1), "=d"(vendor2), "=c"(vendor3), "=a"(largest_supported)
        :"a"(CPUID_VENDOR_ID)
    );

    asm volatile (
        "cpuid\n\t"
        :"=a"(largest_supported_ext)
        :"a"(CPUID_HIGHEST_EXT_FUNC)
    );

    char vendor_id[13];
    memcpy(vendor_id, &vendor1, 4);
    memcpy(vendor_id+4, &vendor2, 4);
    memcpy(vendor_id+8, &vendor3, 4);
    vendor_id[12] = 0;

    kprintf("Kernel: CPU info:\nRunning %s, highest CPUID func 0x%x, extended func 0x%x\n", vendor_id, largest_supported, largest_supported_ext);

// not checking max supported CPUID, because a) if we boot with grub, this becomes 0 for some reason, and b) what we are testing is below the minimal supported anyway

    unsigned long cpu_signature, cpu_feature_flags_1, cpu_feature_flags_2, cpu_additional_features;
    asm volatile (
        "cpuid\n\t"
        :"=a"(cpu_signature), "=d"(cpu_feature_flags_1), "=c"(cpu_feature_flags_2), "=b"(cpu_additional_features)
        :"a"(CPUID_PROCESSOR_INFO_FEATURES)
    );

    unsigned long family_id = 0;
    if (CPUID_1_GET_FAMILY(cpu_signature) != 15) 
        family_id = CPUID_1_GET_FAMILY(cpu_signature);
    else
        family_id = CPUID_1_GET_FAMILY(cpu_signature) + CPUID_1_GET_EXT_FAMILY(cpu_signature);

    if (family_id <= CPUID_PROCESSOR_LIST_LEN) {
        if (memcmp(vendor_id, CPUID_MANUFACTURER_AMD, 12) == 0) {
                if (cpuid_processor_family_ids_amd[family_id] != NULL)
                    kprintf("Family: %s ", cpuid_processor_family_ids_amd[family_id]);
                else goto unknown_name;
        } else if (memcmp(vendor_id, CPUID_MANUFACTURER_INTEL, 12) == 0) {
                if (cpuid_processor_family_ids_intel[family_id] != NULL)
                    kprintf("Family: %s ", cpuid_processor_family_ids_intel[family_id]);
                else goto unknown_name;
        } else goto unknown_name;
    } else {
        unknown_name:
        kprintf("Family: %hhx ", CPUID_1_GET_FAMILY(cpu_signature));
    }

    if (CPUID_1_GET_FAMILY(cpu_signature) == 15 || CPUID_1_GET_FAMILY(cpu_signature) == 6) {
        kprintf("Model: %x ", (CPUID_1_GET_EXT_MODEL(cpu_signature)<<4) + CPUID_1_GET_MODEL(cpu_signature));
    } else {
        kprintf("Model: %hhx ", CPUID_1_GET_MODEL(cpu_signature));
    }
    switch (CPUID_1_GET_TYPE(cpu_signature)) {
        case 0:
            kprintf("OEM ");
            break;
        case 1:
            kprintf("Intel Overdrive ");
            break;
        case 2:
            kprintf("P5 Dual ");
            break;
        case 3:
            kprintf("UNK ");
    }
    kprintf("rev %d\n", CPUID_1_GET_STEPPING(cpu_signature));


    if (largest_supported_ext >= CPUID_VENDOR_FULL_3) {
        uint32_t full_brand_name[12 + 1] = {0}; // 48 ascii string, 1 to be null terminated

        asm volatile (
            "mov %0, %%eax\n\t"
            "cpuid\n\t"
            :"=a"(full_brand_name[0]), "=b"(full_brand_name[1]), "=c"(full_brand_name[2]), "=d"(full_brand_name[3])
            :"a"(CPUID_VENDOR_FULL_1)
        );
        asm volatile (
            "mov %0, %%eax\n\t"
            "cpuid\n\t"
            :"=a"(full_brand_name[4]), "=b"(full_brand_name[5]), "=c"(full_brand_name[6]), "=d"(full_brand_name[7])
            :"a"(CPUID_VENDOR_FULL_2)
        );
        asm volatile (
            "mov %0, %%eax\n\t"
            "cpuid\n\t"
            :"=a"(full_brand_name[8]), "=b"(full_brand_name[9]), "=c"(full_brand_name[10]), "=d"(full_brand_name[11])
            :"a"(CPUID_VENDOR_FULL_3)
        );

        kprintf("Full name: %s\n", (char *)full_brand_name);
    }

    if (CPUID_1_FFLAGS_D_GET_HTT(cpu_feature_flags_1)) {
        kprintf("Kernel: Hyper-threading enabled\n\t%d logical processors installed\n", CPUID_1_GET_ADD_LOGICAL_PROC(cpu_additional_features));
    } else kprintf("Kernel: Hyper-threading not enabled\n");
}


extern struct tss_segment tss;
extern struct idt_gate * idt_descriptor_entries;

unsigned long boot_mem_top = 0; // the top of taken memory (either &_kernel_top, or the highest multiboot module)
void * kernel_mem_top = NULL; // the top of all kernel memory (identity map, heap, page frame table)

void kernel_thread_test(void* _) {
    syscall(SYSCALL_SEM_WAIT, 0);

    kernel_print_cpu_info();

    syscall(SYSCALL_SEM_POST, 0);

    syscall(SYSCALL_EXIT_THREAD, 0);
}

void kernel_entry(multiboot_info_t* mbd, unsigned int magic) {
    boot_mem_top = (uint32_t)&_kernel_top; // the lowest free address
    void * initrd_start = NULL;
    unsigned long initrd_len = 0;
    com_init(0, 115200, 8, 1, COM_PARITY_NONE, COM_BUFFER_1);
    vga_clear();

    kprintf("Running " KERNEL_VERSION ", compiled at "__TIMESTAMP__"\n");

    kernel_entry_addr_log();
    
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        panic("Bootloader didn't return valid physical memory map!");
    }

    kprintf("Kernel cmdline: %s\n", mbd->cmdline);

    if (mbd->mods_count > 0) {
        kprintf("Multiboot mods:\n");
        struct multiboot_mod_list * mods = (struct multiboot_mod_list *)mbd->mods_addr;
        if (mbd->mods_count > 1) kprintf("Multiple multiboot mods detected, assuming last is initrd\n");
        for (int i = 0; i < mbd->mods_count; i++) {
            if (mods[i].mod_end > boot_mem_top) boot_mem_top = mods[i].mod_end;
            kprintf("mod %d 0x%x - 0x%x, cmdline %s\n", i, mods[i].mod_start, mods[i].mod_end, mods[i].cmdline);
            initrd_start = (void*)mods[i].mod_start;
            initrd_len = mods[i].mod_end - mods[i].mod_start;
        }
    }

    uint32_t total_usable = 0;

    for (int i = 0; i < mbd->mmap_length; i+= sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*) (mbd->mmap_addr + i);

        kprintf("Kernel: 0x%lx | Len: 0x%lx | Type: ", mmmt->addr, mmmt->len);
        
        switch (mmmt->type) {
            case MULTIBOOT_MEMORY_AVAILABLE:
                if ((uint64_t)mmmt->addr >= (uint64_t)1<<32) kprintf("Unusable - Requires PAE support");
                else kprintf("Usable");
                break;
            case MULTIBOOT_MEMORY_RESERVED:
                kprintf("Reserved");
                break;
            case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                kprintf("ACPI Reclaimable");
                break;
            case MULTIBOOT_MEMORY_NVS:
                kprintf("Non-Volatile");
                break;
            case MULTIBOOT_MEMORY_BADRAM:
                kprintf("BAD RAM");
                break;
            default:
                kprintf("???UNKNOWN");
        }
        kprintf("\n");

        //if ((uint64_t)mmmt->addr < (uint64_t)1<<32 && mmmt->addr + mmmt->len >= LOWEST_PHYS_ADDR_ALLOWABLE && mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
        if ((uint64_t)mmmt->addr < (uint64_t)1<<32 && mmmt->addr + mmmt->len >= boot_mem_top && mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) { // this approach wastes some memory, but considering we have the kernel directly after LOWEST_PHYS_ADDR_ALLOWABLE, it shouldn't matter
            if ((uint64_t)mmmt->addr + mmmt->len >= (uint64_t)1<<32) {
                total_usable += mmmt->len - (((uint64_t)mmmt->addr + mmmt->len) - ((uint64_t)1<<32));
            //} else if (mmmt->addr < LOWEST_PHYS_ADDR_ALLOWABLE) {
            } else if (mmmt->addr < boot_mem_top) {
                //total_usable += mmmt->len - (mmmt->addr - LOWEST_PHYS_ADDR_ALLOWABLE);
                total_usable += mmmt->len - (boot_mem_top - mmmt->addr);
            } else total_usable += mmmt->len;
        }
    }
    kernel_mem_top = page_frame_alloc_init(mbd, total_usable, (void*)boot_mem_top);
    kprintf("Kernel: Total usable RAM: %u bytes\n", pf_get_free_memory()); //total_usable);

    setup_paging(total_usable, boot_mem_top);

    kernel_print_cpu_info();
    
    construct_descriptor_tables();

    asm volatile ("sti"); // enable software interrupts, but not yet external pic interrupts
    scheduler_init();

    keyboard_init();

    timer_init(0, 1000/KERNEL_TIMER_RESOLUTION_MSEC, TIMER_RATE); // kernel scheduler timer, also enables pic interrupts

    tty_alloc_kernel_console();

    //readelf(initrd_start, initrd_len);
    struct program program = {0};
    program = load_elf(initrd_start, initrd_len);
    if (program.pd_vaddr == NULL) panic("Exec format error!\n");
    scheduler_add_process(program, 3);

    //kalloc_print_heap_objects();

    while (1) {
    }
}
