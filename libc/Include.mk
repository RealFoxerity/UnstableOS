MAKE_ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
LIBC_ROOT := $(abspath $(dir $(realpath $(lastword $(MAKEFILE_LIST)))))

ifndef LIBC_INCLUDE_MK
LIBC_INCLUDE_MK := 1

ifndef PROGRESS_LABEL
PROGRESS_LABEL = echo
endif

# Debug?
ifndef LIBC_DEBUG
ifdef DEBUG
LIBC_DEBUG := $(DEBUG)
else
LIBC_DEBUG := 1
endif
endif

ifeq ($(MAKE_ROOT),$(LIBC_ROOT))
LIBC_BUILD_DIR := $(MAKE_ROOT)/build
SYSROOT        := $(MAKE_ROOT)/build/sysroot
else
LIBC_BUILD_DIR := $(MAKE_ROOT)/build/libc
SYSROOT        := $(MAKE_ROOT)/sysroot
endif

LIBC_CFLAGS := $(CFLAGS) -ffreestanding -nostdlib -nodefaultlibs -std=gnu99 -I$(LIBC_ROOT)/src/include -MMD -MP -fPIC -Wno-prio-ctor-dtor
LIBM_CFLAGS := $(CFLAGS) -std=gnu99 -I$(LIBC_ROOT)/src/include -MMD -MP -fPIC -Wno-prio-ctor-dtor

ifeq ($(DEBUG), 1)
	LIBC_CFLAGS += -Og -g
else
	LIBC_CFLAGS += -O3 -g
endif

LIBC_LIB := $(LIBC_BUILD_DIR)/libc.a
LIBC_SO_LIB := $(LIBC_BUILD_DIR)/libc.so
LIBM_LIB := $(LIBC_BUILD_DIR)/libm.a
LIBM_SO_LIB := $(LIBC_BUILD_DIR)/libm.so

LIBC_INCLUDES := -I$(LIBC_ROOT)/src/include
LIBC_HEADERS := $(shell find $(LIBC_ROOT)/src/include -type f)

LIBC_CRT1  := $(LIBC_BUILD_DIR)/src/crt1.s.o
LIBC_SCRT1 := $(LIBC_BUILD_DIR)/src/Scrt1.s.o

LIBC_SRCS_ALL := $(shell find $(LIBC_ROOT)/src/ -type f -name "*.[cs]" | grep -v src/math/)
LIBC_SRCS_CRT := $(LIBC_ROOT)/src/crt1.s $(LIBC_ROOT)/src/Scrt1.s
LIBC_SRCS := $(filter-out $(LIBC_SRCS_CRT), $(LIBC_SRCS_ALL))
LIBC_OBJS := $(patsubst $(LIBC_ROOT)/%, $(LIBC_BUILD_DIR)/%.o, $(LIBC_SRCS))
LIBC_OBJS_CRT := $(patsubst $(LIBC_ROOT)/%, $(LIBC_BUILD_DIR)/%.o, $(LIBC_SRCS_CRT))

LIBM_SRCS := $(shell find $(LIBC_ROOT)/src/math -type f -name "*.[cs]")
LIBM_OBJS := $(patsubst $(LIBC_ROOT)/%, $(LIBC_BUILD_DIR)/%.o, $(LIBM_SRCS))

.PHONY: rtld $(SYSROOT)
rtld:
	@$(PROGRESS_LABEL) Compiling rtld.so
	@$(MAKE) -C $(LIBC_ROOT)/rtld all

$(SYSROOT): $(LIBC_LIB) $(LIBC_SO_LIB) $(LIBC_OBJS_CRT) $(LIBM_LIB) $(LIBM_SO_LIB) rtld
	@$(PROGRESS_LABEL) Creating sysroot
	# sorry adrian, i really don't know what to do here, you're the makefile magician
	@mkdir -p $(SYSROOT)/usr/lib $(SYSROOT)/mnt $(SYSROOT)/tmp $(SYSROOT)/dev $(SYSROOT)/proc
	@cp -rv $(MAKE_ROOT)/rootfs_files/* $(SYSROOT)/
	@cp -rv $(LIBC_ROOT)/src/include $(SYSROOT)/usr/
	@cp -v $(LIBC_LIB) $(SYSROOT)/usr/lib/
	@cp -v $(LIBM_LIB) $(SYSROOT)/usr/lib/
	@cp -v $(LIBC_SO_LIB) $(SYSROOT)/usr/lib/
	@cp -v $(LIBM_SO_LIB) $(SYSROOT)/usr/lib/
	@cp -v $(LIBC_BUILD_DIR)/src/crt1.s.o $(SYSROOT)/usr/lib/crt1.o
	@cp -v $(LIBC_BUILD_DIR)/src/Scrt1.s.o $(SYSROOT)/usr/lib/Scrt1.o
	@cp -v $(LIBC_ROOT)/rtld/rtld.so $(SYSROOT)/usr/lib/

$(LIBC_LIB): $(LIBC_OBJS)
	@$(PROGRESS_LABEL) Linking $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(AR) rsc $@ $^

$(LIBM_LIB): $(LIBM_OBJS)
	@$(PROGRESS_LABEL) Linking $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(AR) rsc $@ $^

$(LIBC_SO_LIB): $(LIBC_OBJS)
	@$(PROGRESS_LABEL) Linking $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) -fPIC -nostdlib -shared $^ -o $@ -lgcc

$(LIBM_SO_LIB): $(LIBC_SO_LIB) $(LIBM_OBJS)
	@$(PROGRESS_LABEL) Linking $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) -fPIC -shared $(LIBM_OBJS) -o $@ -lgcc -L$(LIBC_BUILD_DIR)

$(LIBC_BUILD_DIR)/%.c.o: $(LIBC_ROOT)/%.c
	@$(PROGRESS_LABEL) Compiling $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(LIBC_BUILD_DIR)/%.s.o: $(LIBC_ROOT)/%.s
	@$(PROGRESS_LABEL) Assembling $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(LIBM_BUILD_DIR)/%.c.o: $(LIBM_ROOT)/%.c
	@$(PROGRESS_LABEL) Compiling $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) $(LIBM_CFLAGS) -c $< -o $@

$(LIBM_BUILD_DIR)/%.s.o: $(LIBM_ROOT)/%.s
	@$(PROGRESS_LABEL) Assembling $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) $(LIBM_CFLAGS) -c $< -o $@

clean::
	@$(MAKE) -C $(LIBC_ROOT)/rtld clean
	@rm -rf $(LIBC_ROOT)/build
	@rm -rf $(SYSROOT)

.PHONY: clean

endif

-include $(LIBC_OBJS:.o=.d)
-include $(LIBM_OBJS:.o=.d)
-include $(LIBC_OBJS_CRT:.o=.d)
