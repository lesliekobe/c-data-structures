# Makefile for Mailbox Kernel Module

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m := mbox_kern.o

EXTRA_CFLAGS := -Wall -Wextra

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

load:
	sudo insmod mbox_kern.ko

unload:
	sudo rmmod mbox_kern

reload:
	sudo rmmod mbox_kern 2>/dev/null; sudo insmod mbox_kern.ko

.PHONY: all clean install load unload reload