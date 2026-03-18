.PHONY: all
all:
	for i in */Makefile; do $(MAKE) -C $$(dirname $$i); done

.PHONY: clean
clean:
	for i in */Makefile; do $(MAKE) -C $$(dirname $$i) clean; done
