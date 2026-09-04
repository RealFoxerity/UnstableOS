AR := ../../toolchain/bin/i686-unstableos-ar
AS := ../../toolchain/bin/i686-unstableos-as
CC := ../../toolchain/bin/i686-unstableos-gcc

CFLAGS := -ffreestanding -nostdlib -nodefaultlibs -nostartfiles -std=gnu99 -g -Og \
 			-fpic -fno-plt \
 			-march=i486 \
 			-I../src/include -fno-stack-protector

RTLD_SRCS := $(shell find ./src/ -type f -name "*.[cs]")
RTLD_OBJS := $(patsubst ./%, ./build/%.o, $(RTLD_SRCS))
RTLD_LIB  := rtld.so

.PHONY: all clean tests

all: $(RTLD_LIB)
$(RTLD_LIB): $(RTLD_OBJS)
	$(CC) $(CFLAGS) $(RTLD_OBJS) -o $(RTLD_LIB) -shared

./build/%.c.o: ./%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

./build/%.s.o: ./%.s
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(RTLD_OBJS) $(RTLD_LIB) lib*.so test

tests:
	$(CC) -I../src/include -shared tests/lib4.c -o lib4.so
	$(CC) -I../src/include -shared tests/lib2.c -o lib2.so -L. -l4
	$(CC) -I../src/include -shared tests/lib1.c -o lib1.so -L. -l2
	$(CC) -I../src/include -shared tests/lib3.c -o lib3.so -L. -l1 -l4
	$(CC) -I../src/include tests/test_exec.c -o test -L. -l3 -l1 -l4  -Wl,-rpath=.

