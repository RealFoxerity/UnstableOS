# UnstableOS
---
A somewhat UNIX-like somewhat POSIX compliant x86 32-bit preemptive multitasking monolithic kernel targeting the i486 feature set written in C and a tiny bit of assembly

> [!WARNING]
> UnstableOS is primarily tested on TCG, there are bound to be issues on real hardware and/or with KVM

## Features
---
- 32-bit paging (without PAE)
- Round-robin scheduler supporting threads
- `open()`, `close()`, `read()`, `write()`, `seek()`, `readdir()`, `dup()`, `dup2()`
- ELF loading & userspace processes
- Ramdisks & Tar as initial ramdisk
- `exec()`, CoW `fork()`, `spawn()`, `wait()`
- Semaphores and kernel spinlocks (technically mutexes)
- Mostly POSIX compliant TTY
- Lame and lacking custom libc\
List of defined syscalls can be found in src/include/kernel.h

## Drivers
---
- PS/2 keyboards
- RS/232 (serial)
- Tar as a filesystem
- RTC
- VGA

## Building
---
Currently you need:
- make
- i686-gcc-elf (and associated liker and assembler)
- tar
- qemu or similar to test\
Then either do `make kernel` to build just the kernel\
or `make all` to create the kernel, utils and memdisk
### Testing in a VM
---
UnstableOS.bin is an ELF image conforming to the Multiboot specification\
So you need to load it as a Multiboot kernel\
The first multiboot module is considered as the initial filesystem\
For qemu you can do (assuming `make all`):\
`qemu-system-i386 -m 100M -kernel build/UnstableOS.bin -initrd build/memdisk.tar`
### Known bugs/issues
---
- very unwieldy way of handling userspace thread creation
- broken RTC 12 hr mode time reading on certain platforms
- Very slow scanf() implementation
- scanf() consumes \n
- scanf() and printf() family of functions don't implement floats
- no locking in page frame allocator making it inherently thread-unsafe
- almost all cases of out of memory are currently handled by kernel panic
- `fork()` (intentionally) doesn't copy any other stack than the calling thread's
- `fork()` sometimes panics on real hardware (increment of free page)
- there is no check if pid exists or not, multiple issues with pid wraparound
- multiple issues with instance fields wraparound in numerous structures
- `wait()` supports only exit status reporting - `WEXITSTATUS()` macro
- `readdir()` is not thread-safe (POSIX doesn't specify whether it has to be)
### TODO in Near Future
---
- [ ] `mmap()`, `munmap()`, `msync()` (only with `MAP_FIXED`)
- [ ] any form of sleep()
- [ ] shared memory (basic IPC)
- [ ] ATA PIO
- [ ] VFAT
- [ ] ATAPI
- [ ] ISO9660
- [ ] sockets/pipes
- [ ] symlinks
- [ ] signals
- [ ] finish implementing sessions and foreground groups for TTY
- [ ] errno in userspace instead of returning negative numbers
- [ ] scheduler priorities?
- [ ] any form of fpu
- [ ] PCI
- [ ] ACPI
- [ ] Ethernet (ne2000)
- [ ] VBE linear framebuffer instead of VGA text mode
- [ ] SMP - multicore support
- [ ] argc, argv, envp + the rest of the exec family
- [ ] Thread-Local Storage + proper errno