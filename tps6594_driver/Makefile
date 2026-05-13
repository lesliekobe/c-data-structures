# Makefile for TPS6594 Driver

# Toolchain (adjust for your environment)
CC = arm-none-eabi-gcc
AR = arm-none-eabi-ar
CFLAGS = -Wall -Wextra -O2 -fno-common
CFLAGS += -mcpu=cortex-r52 -mfloat-abi=hard -mfpu=fpv5-d16
CFLAGS += -ffreestanding -nostdinc
CFLAGS += -I./include

# SafeRTOS paths (adjust as needed)
SAFE_RTOS_DIR = /path/to/safertos
INCLUDE += -I$(SAFE_RTOS_DIR)/include
LDFLAGS = -lm

# Output
LIB_NAME = libtps6594.a

# Source files
SRC_DIR = src
INC_DIR = include

SOURCES = $(SRC_DIR)/tps6594.c $(SRC_DIR)/tps6594_safertos.c

.PHONY: all clean install

all: $(LIB_NAME)

$(LIB_NAME): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDE) -c $(SOURCES)
	$(AR) rcs $@ *.o
	rm -f *.o

# Example build (requires separate main.c)
example: $(LIB_NAME)
	$(CC) $(CFLAGS) $(INCLUDE) examples/main.c $(LIB_NAME) -o example.elf $(LDFLAGS)

clean:
	rm -f $(LIB_NAME) *.o *.elf

install:
	cp $(LIB_NAME) /usr/local/lib/
	cp -r include/* /usr/local/include/

.PHONY: all clean install example