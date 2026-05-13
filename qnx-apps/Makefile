# QNX Application Makefile Template
#
# Usage:
#   make                - build all
#   make clean          - clean all
#   make <target>       - build specific target
#
# Compiler: QNX qcc (QNX Neutrino C compiler)
#

# Compiler and flags
CC = qcc
CFLAGS = -Wall -g -I../common
LDFLAGS = -lpthread -lresmgr

# Targets
HELLO = hello_qnx/hello
RESMGR = resmgr_skeleton/resmgr
PCIE = pcie_access/pcie_bar_test
INTR = interrupt_example/intr_handler
SHMEM = shared_memory/shmem_demo
IPC = qnxIPC/qnxIPC

TARGETS = $(HELLO) $(RESMGR) $(PCIE) $(INTR) $(SHMEM) $(IPC)

.PHONY: all clean $(TARGETS)

all: $(TARGETS)

$(HELLO): hello_qnx/hello_qnx.c ../common/common.c
	$(CC) -o $@ $^ $(CFLAGS)

$(RESMGR): resmgr_skeleton/resmgr_skeleton.c ../common/common.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

$(PCIE): pcie_access/pcie_bar_test.c ../common/common.c
	$(CC) -o $@ $^ $(CFLAGS)

$(INTR): interrupt_example/intr_handler.c ../common/common.c
	$(CC) -o $@ $^ $(CFLAGS)

$(SHMEM): shared_memory/shmem_demo.c ../common/common.c
	$(CC) -o $@ $^ $(CFLAGS)

$(IPC): qnxIPC/qnxIPC.c ../common/common.c
	$(CC) -o $@ $^ $(CFLAGS) -lpthread

clean:
	rm -f $(TARGETS)
	rm -f hello_qnx/hello resmgr_skeleton/resmgr
	rm -f pcie_access/pcie_bar_test interrupt_example/intr_handler
	rm -f shared_memory/shmem_demo qnxIPC/qnxIPC

# Individual targets
hello: $(HELLO)
resmgr: $(RESMGR)
pcie: $(PCIE)
interrupt: $(INTR)
shmem: $(SHMEM)
ipc: $(IPC)