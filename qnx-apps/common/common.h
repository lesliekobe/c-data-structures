/*
 * common.h - Shared definitions for QNX example applications
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#ifndef COMMON_H_
#define COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

/* Debug macro */
#define DBG(fmt, ...) \
    do { if (debug_enabled) fprintf(stderr, "[DBG] " fmt "\n", ##__VA_ARGS__); } while(0)

#define INFO(fmt, ...)  fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...)   fprintf(stderr, "[ERR]  " fmt ": %s\n", ##__VA_ARGS__, strerror(errno))
#define WARN(fmt, ...)  fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)

extern bool debug_enabled;

/* Common return codes */
#define OK      0
#define ERR_FAIL    -1
#define ERR_TIMEOUT -2
#define ERR_INVAL   -3

#endif /* COMMON_H_ */