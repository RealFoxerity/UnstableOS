CC		:=i686-elf-gcc
AS		:=i686-elf-as
LD		:=i686-elf-ld
CFLAGS	:=-ffreestanding -nostdlib -lgcc -nodefaultlibs -DTARGET_I486 -Og -g -Isrc/include
LDFLAGS	:=-T src/linker.ld

OBJS=$(shell find src/ -name "*.[cs]" | grep -vE "crt..s" | sed 's/[cs]$$/o/g')

CRT_OBJS=$(shell find src/ -name "crt*.s" | sed 's/s$$/o/g')

all: $(OBJS) $(CRT_OBJS)
	mkdir -p build
	ar rsc build/libc.a $(OBJS)

.PHONY: clean
clean:
	rm -f $(shell find src/ -name "*.o")
	rm -f build/*