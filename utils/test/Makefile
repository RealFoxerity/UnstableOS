CC		:=i686-elf-gcc
AS		:=i686-elf-as
LD		:=i686-elf-ld
CFLAGS	:=-ffreestanding -static -nostdlib -lgcc -nodefaultlibs -Og -g -isystem ../../libc/src/include -isystem src/include -isysroot ../../
CRTI	:=../../libc/src/crti.o
CRTN	:=../../libc/src/crtn.o
CRT0	:=../../libc/src/crt0.o
CRTBEGIN:=$(shell $(CC) $(CFLAGS) -print-file-name=crtbegin.o)
CRTEND	:=$(shell $(CC) $(CFLAGS) -print-file-name=crtend.o)
LDFLAGS	:=
LIBC	:= ../../libc/build/libc.a

OBJS	:= $(CRT0) $(CRTI) $(CRTBEGIN) src/entry.o $(CRTEND) $(CRTN)

all: $(OBJS) 
	mkdir -p build
	$(CC) $(CFLAGS) $(OBJS) $(LIBC) $(LDFLAGS) -o build/test

.PHONY: clean
clean:
	rm -f build/*
	rm src/entry.o