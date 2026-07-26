MAKE_ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
UTILS_ROOT := $(abspath $(dir $(realpath $(lastword $(MAKEFILE_LIST)))))

ifndef UTILS_INCLUDE_MK
UTILS_INCLUDE_MK := 1

include $(UTILS_ROOT)/../libc/Include.mk

ifndef PROGRESS_LABEL
PROGRESS_LABEL = echo
endif

ifeq ($(MAKE_ROOT),$(UTILS_ROOT))
UTILS_BUILD_DIR := $(MAKE_ROOT)/build
else
UTILS_BUILD_DIR := $(MAKE_ROOT)/build/utils
endif

UTILS := cat clear ls mkdir mount pwd rename rm rmdir setsid sleep stty umount xxd ysh
UTILS_BINS = $(patsubst %, $(UTILS_BUILD_DIR)/%, $(UTILS))

UTILS_CFLAGS := $(CFLAGS) -ffreestanding -static -nostdlib -lgcc -nodefaultlibs -Og -g $(LIBC_INCLUDES) -Isrc/include $(LIBC_LIB) -lgcc

define DEFINE_UTIL_RULE
$(UTILS_BUILD_DIR)/$(1): $$(shell find $(UTILS_ROOT)/$(1)/src/ -type f -name "*.[cs]" 2>/dev/null) $(LIBC_LIB) $(LIBC_CRT0) $(LIBC_CRTI) $(LIBC_CRTN)
	@$$(PROGRESS_LABEL) Building $$(patsubst $$(MAKE_ROOT)/%,%,$$(abspath $$@))
	@mkdir -p $$(dir $$@)
	@files=$$$$(find $(UTILS_ROOT)/$(1)/src/ -type f -name "*.[cs]" 2>/dev/null); \
	$$(CC) $(LIBC_CRT0) $(LIBC_CRTI) $(LIBC_CRTBEGIN) $$$$files $(LIBC_CRTEND) $(LIBC_CRTN) $(UTILS_CFLAGS) -I$(UTILS_ROOT)/$(1)/src/include -o $$@
endef

$(foreach util,$(UTILS),$(eval $(call DEFINE_UTIL_RULE,$(util))))

clean::
	@rm -rf $(UTILS_BUILD_DIR)

.PHONY: clean

endif
