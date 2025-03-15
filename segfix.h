#pragma once
#include <stdio.h>

/* ANSI colour codes */
#define RED   "\x1B[31m"
#define MAG   "\x1B[35m"
#define RESET "\x1B[0m"

/* Library forward definitions */
int segfix_init(char *cmd); // don't call this, use SEGFIX_INIT()

// This is the actual macro that should be called at the beginning of main().
#define SEGFIX_INIT(argc, argv) \
    do { \
        if (!argc) {\
            fprintf(stderr, "Failed to initiate segfix, argument 0 as executable location required.\n"); \
            return 1; \
        } \
        if (segfix_init(argv[0])) {\
            fprintf(stderr, "segfix initiation failed.\n"); \
            return 1; \
        } \
    } while (0)
