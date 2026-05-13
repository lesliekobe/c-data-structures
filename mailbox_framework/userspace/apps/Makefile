# Makefile for Example Applications

CC ?= gcc
CFLAGS := -Wall -g -I../../include
LDFLAGS := -L../lib -lmbox -lpthread

APP := mbox_example

.PHONY: all clean

all: $(APP)

$(APP): ../lib/libmbox.so
	$(CC) $(CFLAGS) -o $@ ../apps/example/*.c $(LDFLAGS)

../lib/libmbox.so:
	$(MAKE) -C ../lib

clean:
	rm -f $(APP) *.o