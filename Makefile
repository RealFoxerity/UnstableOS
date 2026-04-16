CC		:=i686-elf-gcc
AS		:=i686-elf-as
LD		:=i686-elf-ld

# gnu has asm volatile instead of __asm
CFLAGS	:=\
-ffreestanding -T src/linker.ld -nostdlib -nodefaultlibs -nostartfiles -Og -g -std=gnu99 \
-isystem src/include -isystem libc/src/include -isysroot . \
-DTARGET_I486  -Wall -Wno-unknown-pragmas \
-fstack-protector
LDFLAGS	:=-T src/linker.ld -lgcc

OBJS=$(shell find src/ -name "*.[cs]" | sed 's/[cs]$$/o/g') $(shell $(CC) --print-libgcc-file-name)

.PHONY: all kernel
all: kernel utils build/memdisk.tar
kernel: $(OBJS) libc
	mkdir -p build
	$(CC) $(CFLAGS) $(OBJS) libc/build/libc.a -o build/UnstableOS.bin
.PHONY: libc utils
libc:
	$(MAKE) -C libc
utils: libc
	$(MAKE) -C utils

.PHONY: iso build/UnstableOS.iso
iso: build/UnstableOS.iso
build/UnstableOS.iso: all build/memdisk.tar
	mkdir -p build/iso/boot/limine
	cp build/UnstableOS.bin build/memdisk.tar build/iso
	cp limine.conf\
		/usr/share/limine/limine-bios.sys\
		/usr/share/limine/limine-bios-cd.bin\
		build/iso/boot/limine

	mkisofs -b boot/limine/limine-bios-cd.bin\
		-no-emul-boot\
		-r -boot-info-table\
		-o build/UnstableOS.iso build/iso

	limine bios-install build/UnstableOS.iso


.PHONY: clean
clean:
	rm -f $(shell find src/ -name "*.o")
	rm -rf build/*
	$(MAKE) -C libc clean
	$(MAKE) -C utils clean

src/kernel_interrupts.o: src/kernel_interrupts.c
	$(CC) $(CFLAGS) -mgeneral-regs-only -mno-red-zone -c src/kernel_interrupts.c -o src/kernel_interrupts.o

src/kernel_page_fault.o: src/kernel_page_fault.c
	$(CC) $(CFLAGS) -mgeneral-regs-only -mno-red-zone -c src/kernel_page_fault.c -o src/kernel_page_fault.o

src/kernel_syscall.o: src/kernel_syscall.c
	$(CC) $(CFLAGS) -mgeneral-regs-only -mno-red-zone -c src/kernel_syscall.c -o src/kernel_syscall.o

PHONY: build/memdisk.tar
build/memdisk.tar: utils
	mkdir -p build/initmd/bin build/initmd/dev
	cp utils/*/build/* build/initmd/bin/
	cp build/initmd/bin/ysh build/initmd/init
	cp -r utils build/initmd
	tar -C build/initmd -cf build/memdisk.tar init utils bin dev