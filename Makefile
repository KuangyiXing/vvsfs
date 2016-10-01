
all: kernel_mod mkfs.vvsfs truncate view.vvsfs

mkfs.vvsfs: mkfs.vvsfs.c
	gcc -Wall -o $@ $<

truncate: truncate.c
	gcc -Wall -o $@ $<

view.vvsfs: view.vvsfs.c
	gcc -Wall -o $@ $<

ifneq ($(KERNELRELEASE),)
# kbuild part of makefile, for backwards compatibility
include Kbuild

else
# normal makefile
KDIR ?= /home/comp3300/linux-source-3.13.0

kernel_mod:
	$(MAKE) -C $(KDIR) M=$$PWD

endif
