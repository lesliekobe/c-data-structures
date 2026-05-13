# Makefile for Userspace Library

CC ?= gcc
CFLAGS := -Wall -g -I../../include -fPIC
LDFLAGS := -lpthread

LIB := libmbox.so
LIB_A := libmbox.a

.PHONY: all clean

all: $(LIB) $(LIB_A)

$(LIB): mbox_lib.c
	$(CC) $(CFLAGS) -shared -o $@ mbox_lib.c

$(LIB_A): mbox_lib.c
	$(CC) $(CFLAGS) -c -o mbox_lib.o mbox_lib.c
	ar rcs $@ mbox_lib.o
	rm -f mbox_lib.o

clean:
	rm -f $(LIB) $(LIB_A) *.o