#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#include "block/memdisk.h"
#include "debug/backtrace.h"
#include "devs.h"
#include <errno.h>
#include "fs/fs.h"
#include "fs/vfs.h"
#include "kernel_exec.h"
#include "kernel_interrupts.h"
#include "kernel_spinlock.h"
#include "lowlevel.h"
#include "multiboot.h"
#include <string.h>
#include "kernel.h"
#include "mm/kernel_memory.h"
#include "kernel_gdt_idt.h"
#include "ps2_controller.h"
#include "rs232.h"
#include "kernel_sched.h"
#include "timer.h"
#include <unistd.h>
#include <fcntl.h>
#include "kernel_tty_io.h"
#include "gfx.h"
#include "vga.h"
#include "vbe.h"
#include "v8086.h"

// clang is insanely annoying, used because uint32_t is smaller than native pointer size (64 bit int) on my machine
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"

#define tty_write(buf, count) tty_write(buf, count); com_write(1, buf, count);

#define KERNEL_PANIC_MSG "\n\e[0m\e[41m\n##############################\nKernel Panic:\n"
void panic(char * reason) {
    extern spinlock_t gfx_spinlock;
    gfx_spinlock.state = SPINLOCK_UNLOCKED; // in case panic happened during vga writes

    scheduler_lock.state = SPINLOCK_LOCKED; // for future SMP endeavors

    disable_interrupts();
    kprintf(KERNEL_PANIC_MSG);
    unwind_stack();
    kprintf("Reason: %s\e[0m", reason); // SGR reset for serial consoles
    //kalloc_print_heap_objects();
    asm volatile (
        "cli\n\t"
        "hlt\n\t"
    );
    __builtin_unreachable();
}

void kernel_entry_addr_log() {
    kprintf("Kernel loaded at physical address from 0x%p to 0x%p\n", &_kernel_base, &_kernel_top);
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
    [0x6] = "Pentium II+/Core/Atom/Xeon",
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

    kprintf("CPU info:\nRunning %s\nMax CPUID func 0x%lx, ext 0x%lx\n", vendor_id, largest_supported, largest_supported_ext);

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
        kprintf("Family: %hhx ", (char)CPUID_1_GET_FAMILY(cpu_signature));
    }

    if (CPUID_1_GET_FAMILY(cpu_signature) == 15 || CPUID_1_GET_FAMILY(cpu_signature) == 6) {
        kprintf("Model: %lx ", (CPUID_1_GET_EXT_MODEL(cpu_signature)<<4) + CPUID_1_GET_MODEL(cpu_signature));
    } else {
        kprintf("Model: %hhx ", (char)CPUID_1_GET_MODEL(cpu_signature));
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
    kprintf("rev %lu\n", CPUID_1_GET_STEPPING(cpu_signature));


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
        kprintf("Kernel: Hyper-threading enabled\n\t%lu logical processors installed\n", CPUID_1_GET_ADD_LOGICAL_PROC(cpu_additional_features));
    } else kprintf("Kernel: Hyper-threading not enabled\n");
}


extern struct tss_segment tss;
extern struct idt_gate * idt_descriptor_entries;

unsigned long boot_mem_top = 0; // the top of taken memory (either &_kernel_top, or the highest multiboot module)
void * kernel_mem_top = NULL; // the top of all kernel memory (identity map, heap, page frame table)

time_t system_time_sec = 0;
time_t uptime_clicks = 0; // incremented by the RTC


static void idle_func(void * _) {
    while (1) {
        asm volatile ("hlt;");
        reschedule();
    }
}


void kernel_entry(multiboot_info_t* mbd, unsigned int magic) {
    disable_interrupts();

    boot_mem_top = (uint32_t)&_kernel_top; // the lowest free address
    void * initrd_start = NULL;
    unsigned long initrd_len = 0;

    vga_init_graphics(); // preliminary setup to get any gfx output

    com_init(0, 115200, 8, 1, COM_PARITY_NONE, COM_BUFFER_1);

    kprintf("Running " KERNEL_VERSION ", compiled at "__TIMESTAMP__"\n");

    kernel_entry_addr_log();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        kprintf("%p\n", mbd);
        panic("Bootloader didn't return valid physical memory map!");
    }

    kernel_section_header_table = &mbd->u.elf_sec;

    if (kernel_section_header_table->addr == 0)
        kprintf("Warning: No symbol table provided by bootloader\n");

    kprintf("Kernel cmdline: %s\n", (char*)mbd->cmdline);

    if (mbd->mods_count > 0) {
        kprintf("Multiboot mods:\n");
        struct multiboot_mod_list * mods = (struct multiboot_mod_list *)mbd->mods_addr;
        if (mbd->mods_count > 1) kprintf("Multiple multiboot mods detected, assuming last is initrd\n");
        for (int i = 0; i < mbd->mods_count; i++) {
            if (mods[i].mod_end > boot_mem_top) boot_mem_top = mods[i].mod_end;
            kprintf("mod %d 0x%x - 0x%x, cmdline %s\n", i, mods[i].mod_start, mods[i].mod_end, (char*)mods[i].cmdline);
            initrd_start = (void*)mods[i].mod_start;
            initrd_len = mods[i].mod_end - mods[i].mod_start;
        }
    }

    uint32_t total_usable = 0;

    for (int i = 0; i < mbd->mmap_length; i+= sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt = (multiboot_memory_map_t*) (mbd->mmap_addr + i);

        kprintf("Mem: 0x%llx - 0x%llx | Type: ", mmmt->addr, mmmt->addr + mmmt->len);

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
    kprintf("Kernel: Total usable RAM: %lu bytes\n", pf_get_free_memory()); //total_usable);

    // initialize basic stuff
    setup_paging(total_usable, boot_mem_top);
    construct_descriptor_tables();
    enable_interrupts();
    scheduler_init();
    if (kernel_mem_top < VBE_LINEAR_FRAMEBUFFER_START + VBE_LINEAR_FRAMEBUFFER_MAX_SIZE)
        kernel_mem_top = VBE_LINEAR_FRAMEBUFFER_START + VBE_LINEAR_FRAMEBUFFER_MAX_SIZE;

    timer_init(0, 1000/KERNEL_TIMER_RESOLUTION_MSEC, TIMER_RATE); // kernel scheduler timer, also enables pic interrupts
    rtc_init();

    tty_alloc_kernel_console();
    ps2_init();

    init_fds();
    init_inodes();
    init_superblocks();

    dev_initialize_static_devices();

    // assuming that by some miracle the gpu doesn't support vga emulation,
    // no text will be visible up until this point
    vbe_gather_info();

    kernel_print_cpu_info();

    if (initrd_start + initrd_len > (void*)IDENT_MAPPING_MAX_ADDR)
        panic("initrd too large");

    // considering how we do file descriptors, it's guaranteed these will be 0, 1, 2
    kassert(open_raw_device_fd(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), O_RDWR) >= 0);
    kassert(open_raw_device_fd(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), O_RDWR) >= 0);
    kassert(open_raw_device_fd(GET_DEV(DEV_MAJ_TTY, DEV_TTY_CONSOLE), O_RDWR) >= 0);

    dev_t initrd_memdisk = 0;
    kassert((initrd_memdisk = memdisk_from_range(initrd_start, initrd_len) != (dev_t)-1));

    int initrd_fd = -1;
    kassert((initrd_fd = open_raw_device_fd(GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK0), O_RDONLY)) >= 0);

    mount_root(GET_DEV(DEV_MAJ_MEM, DEV_MEM_MEMDISK0), FS_TARFS, 0);

    inode_t * dev_inode = NULL;
    if (openat_inode(current_process->root, "/dev", O_DIRECTORY | O_RDONLY, 0, &dev_inode) < 0) {
        kprintf("No /dev directory in initial memdisk, /dev won't be mounted\n");
    } else {
        if (mount_dev(-1, dev_inode, FS_DEVFS, 0) != 0) {
            kprintf("Error while mounting /dev, /dev won't be mounted\n");
        }
        close_inode(dev_inode);
    }

    switch (sys_spawn("/init", (char *[]){"/init", "root=memdisk", NULL}, (char * []){"PATH=/bin:/sbin", "PWD=/", "HOME=/",NULL})) {
        case 1: break; // success, pid 1
        case-ENOEXEC: panic("Exec format error on init process!");
        case-ENOENT: panic("Failed to locate /init!");
        case-EISDIR: panic("/init is a directory!");
        default:
            panic("Failed to load /init!\n");
    }

    enable_interrupts();


    idle_task = kernel_create_thread(current_process, idle_func, NULL);
    current_thread->status = SCHED_UNINTERR_SLEEP;
    while (1) {
        // kernel thread serves as the idle task
        asm volatile("hlt;");
        reschedule();
    }
}
