/* This file takes an argument and throws a segfault with common mistakes for testing of the library */
#include "segfix.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void cause_rodata_write_segfault() {
    char *s = "This string will be stored in .rodata (read only, should segfault if written to)";
    s[0] = s[1];
}

void cause_null_pointer_segfault() {
    char *addr = NULL;
    *addr = 'A';
}

void cause_stackoverflow() {
    cause_stackoverflow();
}

int main(int argc, char **argv) {
    SAFEC_INIT(argc, argv);
    if (argc != 2) {
        printf("Too many or not enough arguments supplied to test program, expected one. "
               "(second argument can either be \"nullpointer\", \"write_rodata\", or \"stackoverflow\")\n");
        return 1;
    }
    if (!strcmp(argv[1], "nullpointer"))
        cause_null_pointer_segfault();
    else if (!strcmp(argv[1], "write_rodata"))
        cause_rodata_write_segfault();
    else if (!strcmp(argv[1], "stackoverflow"))
        cause_stackoverflow();
    else {
        printf("Unknown second argument to describe test segfault cause.\n");
        return 1;
    }
}
