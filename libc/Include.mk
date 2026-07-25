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
else
LIBC_BUILD_DIR := $(MAKE_ROOT)/build/libc
endif

LIBC_CFLAGS := $(CFLAGS) -ffreestanding -nostdlib -nodefaultlibs -std=gnu99 -I$(LIBC_ROOT)/src/include

ifeq ($(DEBUG), 1)
	LIBC_CFLAGS += -Og -g
else
	LIBC_CFLAGS += -O3 -g
endif

LIBC_LIB := $(LIBC_BUILD_DIR)/libc.a
LIBC_INCLUDES := -I$(LIBC_ROOT)/src/include
LIBC_HEADERS := $(shell find $(LIBC_ROOT)/src/include -type f)

LIBC_CRTBEGIN := $(shell $(CC) $(LIBC_CFLAGS) -print-file-name=crtbegin.o)
LIBC_CRTEND := $(shell $(CC) $(LIBC_CFLAGS) -print-file-name=crtend.o)
LIBC_CRT0 := $(LIBC_BUILD_DIR)/src/crt0.s.o
LIBC_CRTI := $(LIBC_BUILD_DIR)/src/crti.s.o
LIBC_CRTN := $(LIBC_BUILD_DIR)/src/crtn.s.o

LIBC_SRCS_ALL := $(shell find $(LIBC_ROOT)/src/ -type f -name "*.[cs]")
LIBC_SRCS_CRT := $(filter $(LIBC_ROOT)/src/crt%s, $(LIBC_SRCS_ALL))
LIBC_SRCS := $(filter-out $(LIBC_ROOT)/src/crt%s, $(LIBC_SRCS_ALL))
LIBC_OBJS := $(patsubst $(LIBC_ROOT)/%, $(LIBC_BUILD_DIR)/%.o, $(LIBC_SRCS))
LIBC_OBJS_CRT := $(patsubst $(LIBC_ROOT)/%, $(LIBC_BUILD_DIR)/%.o, $(LIBC_SRCS_CRT))

$(LIBC_LIB): $(LIBC_OBJS)
	@$(PROGRESS_LABEL) Linking $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@ar rsc $@ $^

$(LIBC_BUILD_DIR)/%.c.o: $(LIBC_ROOT)/%.c
	@$(PROGRESS_LABEL) Compiling $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) $(LIBC_CFLAGS) -c $< -o $@

$(LIBC_BUILD_DIR)/%.s.o: $(LIBC_ROOT)/%.s
	@$(PROGRESS_LABEL) Assembling $(patsubst $(MAKE_ROOT)/%,%,$(abspath $@))
	@mkdir -p $(dir $@)
	@$(CC) $(LIBC_CFLAGS) -c $< -o $@

clean::
	@rm -rf $(LIBC_ROOT)/build

.PHONY: clean

endif