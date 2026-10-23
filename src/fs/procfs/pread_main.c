#include "mm/kernel_memory.h"
#include "mm/pmm.h"
#include "kernel_spinlock.h"
#include "kernel_sched.h"
#include "rbtree.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "procfs.h"
#include "lowlevel.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#define PROCFS_BUFSIZ 257
#define CPUINFO_UNK "Unknown CPU"
// a copy of what's in kernel.c
static size_t get_cpuinfo(char * buf) {
    size_t written = 0;
    if (!is_cpuid_supported()) {
        strcpy(buf, CPUINFO_UNK);
        return sizeof(CPUINFO_UNK);
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

    written += snprintf(
        buf + written, PROCFS_BUFSIZ  - written,
        "CPU info:\nRunning %s\nMax CPUID func 0x%lx, ext 0x%lx\n",
        vendor_id, largest_supported, largest_supported_ext);

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
                if (family_id <= 32 && cpuid_processor_family_ids_amd[family_id] != NULL)
                    written += snprintf(
                        buf + written,
                        PROCFS_BUFSIZ - written,
                        "Family: %s ", cpuid_processor_family_ids_amd[family_id]);
                else goto unknown_name;
        } else if (memcmp(vendor_id, CPUID_MANUFACTURER_INTEL, 12) == 0) {
                if (family_id <= 32 && cpuid_processor_family_ids_intel[family_id] != NULL)
                    written += snprintf(
                        buf + written,
                        PROCFS_BUFSIZ - written,
                        "Family: %s ", cpuid_processor_family_ids_intel[family_id]);
                else goto unknown_name;
        } else goto unknown_name;
    } else {
        unknown_name:
        written += snprintf(
            buf + written,
            PROCFS_BUFSIZ - written,
            "Family: %hhx ", (char)CPUID_1_GET_FAMILY(cpu_signature));
    }

    if (CPUID_1_GET_FAMILY(cpu_signature) == 15 || CPUID_1_GET_FAMILY(cpu_signature) == 6) {
        written += snprintf(
            buf + written,
            PROCFS_BUFSIZ - written,
            "Model: %lx ", (CPUID_1_GET_EXT_MODEL(cpu_signature)<<4) + CPUID_1_GET_MODEL(cpu_signature));
    } else {
        written += snprintf(
            buf + written,
            PROCFS_BUFSIZ - written,
            "Model: %hhx ", (char)CPUID_1_GET_MODEL(cpu_signature));
    }
    switch (CPUID_1_GET_TYPE(cpu_signature)) {
        case 0:
            written += snprintf(
                buf + written,
                PROCFS_BUFSIZ - written,
                "OEM ");
            break;
        case 1:
            written += snprintf(
                buf + written,
                PROCFS_BUFSIZ - written,
                "Intel Overdrive ");
            break;
        case 2:
            written += snprintf(
                buf + written,
                PROCFS_BUFSIZ - written,
                "P5 Dual ");
            break;
        case 3:
            written += snprintf(
                buf + written,
                PROCFS_BUFSIZ - written,
                "UNK ");
    }
    written += snprintf(
        buf + written,
        PROCFS_BUFSIZ - written,
        "rev %lu\n", CPUID_1_GET_STEPPING(cpu_signature));


    if (largest_supported_ext >= CPUID_VENDOR_FULL_3) {
        uint32_t full_brand_name[12 + 1] = {0}; // 48 ascii string, 1 to be null terminated

        asm volatile (
            "cpuid\n\t"
            :"=a"(full_brand_name[0]), "=b"(full_brand_name[1]), "=c"(full_brand_name[2]), "=d"(full_brand_name[3])
            :"a"(CPUID_VENDOR_FULL_1)
        );
        asm volatile (
            "cpuid\n\t"
            :"=a"(full_brand_name[4]), "=b"(full_brand_name[5]), "=c"(full_brand_name[6]), "=d"(full_brand_name[7])
            :"a"(CPUID_VENDOR_FULL_2)
        );
        asm volatile (
            "cpuid\n\t"
            :"=a"(full_brand_name[8]), "=b"(full_brand_name[9]), "=c"(full_brand_name[10]), "=d"(full_brand_name[11])
            :"a"(CPUID_VENDOR_FULL_3)
        );

        written += snprintf(
            buf + written,
            PROCFS_BUFSIZ - written,
            "Full name: %s\n", (char *)full_brand_name);
    }
    return written;
}

ssize_t procfs_pread_main(file_descriptor_t * fd, void * buf, size_t n, off_t offset) {
    kassert(fd);
    kassert(fd->inode);
    if (!S_ISREG(fd->inode->mode) && !S_ISLNK(fd->inode->mode))
        return -EINVAL;
    if (INO_GET_FOLDER(fd->inode->id))
        return -EINVAL;
    if (!buf)
        return -EFAULT;
    if (n == 0)
        return 0;
    size_t len = 0;

    if (INO_GET_FILE(fd->inode->id) == PROCFS_cmdline) {
        len = strlen(kernel_cmdline);
        if (offset >= len)
            return 0;
        if (offset + n > len)
            n = len - offset;
        memcpy(buf, kernel_cmdline + offset, n);
        return (ssize_t)n;
    }

    // guaranteed no file here exceeds 256 so better to quickly discard the request
    if (offset < 0 || offset >= PROCFS_BUFSIZ)
        return 0;
    time_t uptime_frac = uptime_clicks % RTC_TIMER_RESOLUTION_HZ;
    uptime_frac *= 1000;
    uptime_frac /= RTC_TIMER_RESOLUTION_HZ;
    char temp[PROCFS_BUFSIZ];
    switch (INO_GET_FILE(fd->inode->id)) {
        case PROCFS_meminfo:
            len = snprintf(temp, PROCFS_BUFSIZ,
"MemTotal: %10zu kB\n"
"MemFree:  %10zu kB\n",
            pmm_get_usable_page_count() * (PAGE_SIZE / 1024),
            pmm_get_free_page_count() * (PAGE_SIZE / 1024));
            break;
        case PROCFS_uptime:
            len = snprintf(temp, PROCFS_BUFSIZ, "%lld.%03lld\n",
                uptime_clicks / RTC_TIMER_RESOLUTION_HZ,
                uptime_frac);
            break;
        case PROCFS_version:
            len = snprintf(temp, PROCFS_BUFSIZ, KERNEL_VERSION " (compiled with love at "__TIMESTAMP__")\n");
            break;
        case PROCFS_cpuinfo:
            len = get_cpuinfo(temp);
            break;
        case PROCFS_self:
            len = snprintf(temp, PROCFS_BUFSIZ, "%ld", current_process->pid);
            break;
        default:
            return -EINVAL;
    }

    if (offset >= len)
        return 0;
    if (offset + n > len)
        n = len - offset;
    memcpy(buf, temp + offset, n);
    return (ssize_t)n;
}