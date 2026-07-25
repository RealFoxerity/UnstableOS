ifneq ($(words $(MAKECMDGOALS)),1)
.DEFAULT_GOAL = all
%:
	@$(MAKE) $@ --no-print-directory -rRf $(firstword $(MAKEFILE_LIST))
else

ifndef PROGRESS_LABEL
PROG_TOTAL != ${MAKE} ${MAKECMDGOALS} --dry-run PROGRESS_LABEL="PROG_SENTINEL" | grep -c "PROG_SENTINEL"

ifeq ($(PROG_TOTAL),0)
PROGRESS_LABEL = :
else
PROG_COUNT = $(eval PROG_N != expr ${PROG_N} + 1)${PROG_N}
PROGRESS_LABEL = echo "[`expr ${PROG_COUNT} '*' 100 / ${PROG_TOTAL}`%]"
endif
endif

# Global CFLAGS that apply to everything
CFLAGS += -march=i486

DEBUG ?= 1

ifeq (DEBUG,1)
	CFLAGS += -Og -g
else
	CFLAGS += -O3 -g
endif

include libc/Include.mk
include utils/Include.mk

KERNEL_CFLAGS := $(CFLAGS) 	-ffreestanding -nostdlib -nodefaultlibs \
	-nostartfiles -std=gnu99 -Isrc/include $(LIBC_INCLUDES) \
	-Wall -Wno-unknown-pragmas -fno-strict-aliasing -fstack-protector -march=i486

KERNEL_LDFLAGS := -T src/linker.ld $(LIBC_LIB) -lgcc

SRCS := $(shell find src/ -type f -name "*.[cs]")
OBJS := $(patsubst %, build/%.o, $(SRCS))

all: build

build: iso

kernel: build/UnstableOS.bin

iso: build/UnstableOS.iso

clean::
	@rm -rf build

run: build/hda.dd
	qemu-system-i386 -no-shutdown -no-reboot -m 64M -cpu 486 -kernel build/UnstableOS.bin -initrd build/memdisk.tar -display sdl -serial stdio -hda build/hda.dd -vga cirrus
	#qemu-system-i386 -no-shutdown -no-reboot -m 64M -cdrom build/UnstableOS.iso -display sdl -serial stdio -hda build/hda.dd -vga cirrus

.PHONY: FORCE default all build kernel iso clean run
FORCE:

# ------------------------------------------------------------
# Anything beyond this point is NOT completely refactored!

build/UnstableOS.bin: $(LIBC_LIB) $(OBJS)
	@$(PROGRESS_LABEL) Linking $@
	@mkdir -p build
	@$(CC) $(KERNEL_CFLAGS) $(OBJS) -o $@ $(KERNEL_LDFLAGS)

build/UnstableOS.iso: build/UnstableOS.bin build/memdisk.tar
	@$(PROGRESS_LABEL) Generating $@
	@mkdir -p build/iso/boot/limine
	@cp build/UnstableOS.bin build/memdisk.tar build/iso
	@cp limine.conf\
		/usr/share/limine/limine-bios.sys\
		/usr/share/limine/limine-bios-cd.bin\
		build/iso/boot/limine

	@mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-r -boot-info-table \
		-o build/UnstableOS.iso build/iso \
		-quiet 2> /dev/null

	@limine bios-install build/UnstableOS.iso 2> /dev/null

build/memdisk.tar: $(LIBC_HEADERS) $(UTILS_BINS)
	@$(PROGRESS_LABEL) Generating $@
	@mkdir -p build/initmd/bin build/initmd/dev build/initmd/usr/include
	@cp -r $(LIBC_HEADERS) build/initmd/usr/include/
	@cp $(UTILS_BINS) build/initmd/bin/
	@cp build/initmd/bin/ysh build/initmd/init
	@tar -C build/initmd -cf $@ init bin dev usr

build/hda.dd:
	@$(PROGRESS_LABEL) Generating $@
	@mkdir -p $(dir $@)
	@dd if=/dev/zero of=build/hda.dd bs=50M count=1

build/%.c.o: %.c
	@$(PROGRESS_LABEL) Compiling $@
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/%.s.o: %.s
	@$(PROGRESS_LABEL) Assembling $@
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/src/kernel_interrupts.c.o: src/kernel_interrupts.c
	@$(PROGRESS_LABEL) Compiling $@
	@$(CC) $(KERNEL_CFLAGS) -mgeneral-regs-only -mno-red-zone -c $< -o $@

build/src/kernel_page_fault.c.o: src/kernel_page_fault.c
	@$(PROGRESS_LABEL) Compiling $@
	@$(CC) $(KERNEL_CFLAGS) -mgeneral-regs-only -mno-red-zone -c $< -o $@

build/src/kernel_syscall.c.o: src/kernel_syscall.c
	@$(PROGRESS_LABEL) Compiling $@
	@$(CC) $(KERNEL_CFLAGS) -mgeneral-regs-only -mno-red-zone -c $< -o $@

endif