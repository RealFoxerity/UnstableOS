# UnstableOS
---
A somewhat UNIX-like somewhat POSIX compliant x86 32-bit preemptive multitasking monolithic kernel targeting the i486 feature set written in C and a tiny bit of assembly

> [!WARNING]
> UnstableOS is primarily tested on TCG, there are bound to be issues on real hardware and/or with KVM\
It is not meant as a production OS, there is no testing, there is no fuzzing. I try to make it safe, but I am not perfect, anything is possible...

## Features
---
- 32-bit paging (without PAE)
- Round-robin scheduler supporting threads
- Standard VFS functions you'd expect
- ELF loading & userspace processes
- Ramdisks & Tar as initial ramdisk
- `exec()`, CoW `fork()`, `spawn()`, `wait()`
- Signals - most of `sig*`, `kill()`, `tgkill()`
- Semaphores and kernel spinlocks (technically mutexes)
- Mostly POSIX compliant TTY
- DEC VT102 inspired framebuffer console
- Lame and lacking custom libc

List of defined syscalls can be found in [kernel.h](./src/include/kernel.h)

## Drivers
---
- PS/2 keyboards and mice
- RS/232 (serial)
- Tar as a filesystem
- mountable devtmpfs
- RTC
- VGA

## Building
---
Currently you need:
- make
- i686-gcc-elf (and associated liker and assembler)
- tar
- qemu (or similar to test)

For making an ISO, you additionally need:
- limine
- cdrtools (or something providing mkisofs)

Then either do `make kernel` to build just the kernel,\
`make all` to create the kernel, utils and memdisk, or\
`make iso` to create an iso from the kernel, utils and memdisk
### Testing in a VM
---
UnstableOS.bin is an ELF image conforming to the Multiboot specification\
So you need to load it as a Multiboot kernel\
\
The first multiboot module is considered as the initial filesystem\
For qemu you can do (assuming `make iso`):\
`qemu-system-i386 -m 100M -cdrom build/UnstableOS.iso`
### Known bugs/issues/quirks
---
see [caveats.md](./caveats.md) for info
### TODO in Near Future
---
- [ ] `mmap()`, `munmap()`, `msync()` (only with `MAP_FIXED`)
- [x] `stat()`
- [ ] `mkdir()`, `rmdir()`
- [ ] `unlink()`
- [x] monotonic clock `sleep()`/`nanosleep()` using the RTC clock
- [ ] the rest of clock types and waiting functions
- [ ] some missing `time.h` functions
- [ ] HPET timer
- [ ] shared memory (basic IPC)
- [ ] ATA PIO
- [ ] VFAT
- [ ] ATAPI
- [ ] ISO9660
- [x] pipes (see [caveats.md](./caveats.md))
- [ ] sockets and named pipes
- [ ] symlinks
- [x] signals
- [ ] proper (linked) `ucontext_t`
- [ ] signal alternative stacks
- [ ] implement priority for realtime signals 
- [x] `waitid()`, `waitpid()`
- [ ] `alarm()`
- [ ] finish implementing sessions and foreground groups for TTY
- [x] errno in userspace instead of returning negative numbers
- [ ] scheduler priorities?
- [ ] any form of fpu
- [ ] PCI
- [ ] ACPI
- [ ] Ethernet (ne2000)
- [x] Virtual-8086 mode
- [x] VBE linear framebuffer instead of VGA text mode
- [ ] proper VBE reading
- [ ] SMP - multicore support
- [x] argv, argc, envp/environ, execve
- [ ] functional `execve()` and `spawn()` for ring 0 processes
- [ ] auxiliary vector (for elf interpreters)
- [ ] Thread-Local Storage + proper errno
- [ ] Core utils
- [ ] proper POSIX threads implementation