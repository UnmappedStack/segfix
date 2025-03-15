#pragma once

/* ANSI colour codes */
#define RED   "\x1B[31m"
#define MAG   "\x1B[35m"
#define RESET "\x1B[0m"

/* Library forward definitions */
int segfix_init(char *cmd); // don't call this, use SAFEC_INIT()

// This is the actual macro that should be called at the beginning of main().
#define SAFEC_INIT(argc, argv) \
    do { \
        if (!argc) {\
            printf("Failed to initiate segfix, argument 0 as executable location required.\n"); \
            return 1; \
        } \
        if (segfix_init(argv[0])) {\
            printf("segfix initiation failed.\n"); \
            return 1; \
        } \
    } while (0)
