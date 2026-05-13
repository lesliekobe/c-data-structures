/*
 * hello_qnx.c - Minimal QNX Neutrino hello world
 *
 * Demonstrates basic QNX program structure: main, process creation.
 *
 * Build: qcc -o hello hello_qnx.c
 * Run: ./hello
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("Hello from QNX Neutrino!\n");
    printf(" argc=%d, argv[0]=%s\n", argc, argv[0]);

    /* Get process info */
    printf(" PID=%d, PPID=%d\n", getpid(), getppid());

    /* Sleep demonstration - common QNX pattern */
    printf("Sleeping 1 second...\n");
    sleep(1);

    printf("Done.\n");
    return 0;
}