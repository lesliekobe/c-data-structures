# Makefile for Xilinx PCIe EP out-of-tree kernel module

# Kernel build directory - update this to your kernel source tree
KDIR ?= /lib/modules/$(shell uname -r)/build

# Compiler flags
EXTRA_CFLAGS := -Wall -O2
EXTRA_CFLAGS += -DDEBUG

obj-m := xilinx_pcie_ep.o

# Default target: build module
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean build artifacts
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Install module
install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

# Build for specific kernel version
build:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

.PHONY: all clean install build