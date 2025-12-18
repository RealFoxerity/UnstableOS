CC		:=i686-elf-gcc
AS		:=i686-elf-as
LD		:=i686-elf-ld
 
# gnu has asm volatile instead of __asm
CFLAGS	:=\
-ffreestanding -T src/linker.ld -nostdlib -lgcc -nodefaultlibs -nostartfiles -Og -g -std=gnu99 \
-isystem src/include -isystem libc/src/include -isysroot . \
-DTARGET_I486  -Wall -Wno-unknown-pragmas \
-fstack-protector
LDFLAGS	:=-T src/linker.ld

OBJS=$(shell find src/ -name "*.[cs]" | sed 's/[cs]$$/o/g')

all: $(OBJS) libc utils
	$(CC) $(CFLAGS) $(OBJS) libc/build/libc.a -o build/UnstableOS.bin
.PHONY: libc utils
libc:
	$(MAKE) -C libc
utils:
	$(MAKE) -C utils

.PHONY: clean
clean:
	rm -f $(shell find src/ -name "*.o")
	rm -f build/*
	$(MAKE) -C libc clean
	$(MAKE) -C utils clean
	
src/kernel_interrupts.o: src/kernel_interrupts.c
	$(CC) $(CFLAGS) -mgeneral-regs-only -mno-red-zone -c src/kernel_interrupts.c -o src/kernel_interrupts.o


src/kernel_syscall.o: src/kernel_syscall.c
	$(CC) $(CFLAGS) -mgeneral-regs-only -mno-red-zone -c src/kernel_syscall.c -o src/kernel_syscall.o