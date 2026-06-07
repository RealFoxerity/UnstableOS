#ifndef KERNEL_H
#define KERNEL_H
#include <stdint.h>
#include <stdio.h>
#include <UnstableOS/devs.h>

#define KERNEL_TIMER_RESOLUTION_MSEC 4
#define RTC_TIMER_RESOLUTION_HZ 1024
#define RTC_TIME_RESOLUTION_USEC (1000000 / RTC_TIMER_RESOLUTION_HZ)

#define KERNEL_VERSION "UnstableOS v0.01"

#define __STR_INNER(x) #x
#define STR(x) __STR_INNER(x)

#define kabort() {asm volatile ("mov %0, %%eax; int $" STR(SYSCALL_INTERR) :: "r"(SYSCALL_ABORT)); asm volatile ("1:; hlt; jmp 1b");} // hlt loop if interrupts were disabled and thus syscall wouldn't fire

// do {...} while (0); to work in if something; else something;

// most probably it doesn't matter anyway, so just kill the kernel at that point...
#define kassert(cond) do {\
    if (!(cond)) {\
        char errmsg[128];\
        sprintf(errmsg, "Kernel assertion `"#cond"` failed in %s()! [" __FILE__ ":" STR(__LINE__) "]\n", __func__);\
        panic(errmsg);\
    }\
} while (0)

#define UNLINK_DOUBLE_LINKED_LIST(item, list) do {  \
    if (item->next != NULL)                         \
        item->next->prev = item->prev;              \
    else                                            \
        list->prev = item->prev;                    \
    if (item != list)                               \
        item->prev->next = item->next;              \
    else                                            \
        list = item->next;                          \
} while (0);

#define APPEND_DOUBLE_LINKED_LIST(item, list) do {  \
    item->next = NULL;                              \
    if (list == NULL) {                             \
        list = item;                                \
    } else {                                        \
        item->prev = list->prev;                    \
        list->prev->next = item;                    \
    }                                               \
    list->prev = item;                              \
} while (0);

#include <UnstableOS/syscalls.h>

extern unsigned long _kernel_base, _kernel_top, _kernel_stack_top, boot_mem_top;
extern time_t system_time_sec, uptime_clicks;

void __attribute__((format(printf, 1, 2))) kprintf(const char *format, ...);

void __attribute__((noreturn)) panic(char * reason);

void kernel_reset_system(); // kernel_ps2.c

dev_t dev_get_ephemeral();

/****** feature macros ******/
#define HEAP_POISONING // fills freed chunks with 0x41 and allocated with 0x62

/*
numerous reasons to reschedule:
avoid kernel starvation by a syscall spamming thread
make signals to a *process* forced

since we are already in ring 0, the penalty for calling reschedule is almost zero

however, in a thread that uses syscall very frequently (e.g. writing to a framebuffer)
this would make it extremely slow
if not selected, reschedule only happens on cleanup and non-running thread states (sleep)
*/
//#define SYSCALLS_RESCHEDULE

// most devices start at sc2 and need lookup tables to convert to sc1
// some devices allow to directly set them as sc1, meaning we can skip the conversion
#define PS2_TRY_TO_NEGOTIATE_SC1
#define PS2_MOUSE_PACKET_SPEED 40 // per second; can be 10, 20, 40, 80, 100, 200
//#define PS2_MOUSE_LINUX_COMPAT // makes the psaux device work as it does on linux - 3 bytes; no scroll wheel/5 buttons

// assuming the monitor and gpu is from at least 1994, it should support DDC/EDID
// in cases it doesn't, we can either give up, or assume it's a virtual monitor/gpu that doesn't implement DDC/EDID
// e.g. QEMU qxl vga device
#define VBE_EDID_ASSUME_VIRTUAL_ON_FAILURE

// upper limit for the BGA resolution so we don't set something like 16000x12000
#define BGA_MAX_ALLOWABLE_XRES 1920
#define BGA_MAX_ALLOWABLE_YRES 1080

// if defined, reading/writing to /dev/fb calls current_video_funcs->read/write
// if not defined, raw reading from the mapped framebuffer
//#define FB_ACCESS_CALLS_GFX_API

/* POSIX wants EINVAL on invalid request of any kind, and
 * ENOTTY for non-STREAMS based devices
 *
 * Linux does EINVAL on completely invalid requests,
 * ENOTTY for requests that are normally valid, but not for this kind of device, and
 * ENOTTY for non-STREAMS based devices
 */
//#define POSIX_LIKE_IOCTL_ERRORS

// linux and similar just truncate the read/write size to 0x7ffff000
// we can either do the same, or return -E2BIG on such ints
//#define E2BIG_ON_2G
#endif