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

default: all

include Include.mk

all: $(LIBC_LIB) $(LIBC_OBJS_CRT)

.PHONY: default all

endif