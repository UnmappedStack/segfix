/* Main source file for segfix, my custom C segmentation fault handler.
 * Copyright (C) 2025 Jake Steinburger (UnmappedStack) under the Mozilla Public License 2.0. */
#define _GNU_SOURCE
#include "segfix.h"
#include "list.h"
#include <sys/auxv.h>
#include <sys/ptrace.h>
#include <ucontext.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <link.h>

#define ALT_STACK_SIZE 4096 * 10 // 10 pages

#if defined(__PIE__) || defined(__PIC__)
    #error("PIE or PIC is enabled, pelase disable it for debug executables with segfix (-no-pie -fno-pie)")
#endif

/* Define structures */
// Linked list node for read only sections
typedef struct {
    struct list list;
    void *start;
    void *end;
} ReadOnlySection;

// A single stack frame for stack unwinding
typedef struct StackFrame StackFrame;
struct StackFrame {
    StackFrame* rbp;
    uint64_t rip;
};

// All global information that segfix uses
typedef struct {
    int is_initiated;
    ReadOnlySection rosections;
    char *cmd;
    void *entry_rbp;
} segfixInfo;

segfixInfo global_info = {0};

/* simple error handler which takes an error message, prints it, and exits */
__attribute__((noreturn))
void err(char *msg) {
    fprintf(stderr, msg);
    exit(1);
}

/* reads the stack from before rsp was switched to the segfault handler stack, and
 * move the low and high addresses of it into the buffers **start and **end */
void get_original_stack(size_t *start, size_t *end) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        err("Failed to get stack location in get_original_stack() (segfix exception handler)\n");
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, "[stack]")) continue;
        if (sscanf(line, "%p-%p", (void**) start, (void**) end) == 2) break;
        fclose(fp);
        err("scanf failed in segfix exception handler (get_original_stack)\n");
    }
    fclose(fp);
}

/* this is what dl_iterate_phdr() calls to initiate the list of read-only memory sections */
static int callback(struct dl_phdr_info *info, size_t size, void *data) {
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
        if (!(phdr->p_type == PT_LOAD && (phdr->p_flags & PF_R) && !(phdr->p_flags & PF_W))) continue;
        // Read only segment found, append it to the list of read only sections
        ReadOnlySection *new_section = (ReadOnlySection*) malloc(sizeof(ReadOnlySection));
        if (!new_section)
            err("Failed to allocate memory for read only section list element (segfix initiation: callback()\n");
        new_section->start = (void *)(info->dlpi_addr + phdr->p_vaddr);
        new_section->end   = (void *)(info->dlpi_addr + phdr->p_vaddr + phdr->p_memsz);
        list_insert(&global_info.rosections.list, &new_section->list);
    }
    return 0;
}

/* individual checks for causes of the segfault */
void check_readonly_memory_issue(siginfo_t *si, ucontext_t *ucontext) {
    for (struct list *entry = global_info.rosections.list.next;
            entry != &global_info.rosections.list;
            entry = entry->next) {
        ReadOnlySection *section = (ReadOnlySection*) entry;
        int addr_in_rosection = si->si_addr <= section->end && si->si_addr >= section->start;
        if (!addr_in_rosection) continue;
        err(MAG "\nIssue found, wrote to read-only memory section.\n\n" RESET
               "Programs have sections in memory, such as sections where the machine code is and where the data is.\n"
               "When you write read-only sections, segmentation faults can occur. A common cause for this is writing to a string literal, as strings are stored in read only data.\n\n"
               "To fix this, make sure that all of your string literals are copied onto the heap or a similar solution before writing to them.\n\n");
    }
}

void check_nullpointer_issue(siginfo_t *si, ucontext_t *ucontext) {
    if (si->si_addr < (void*) 10) {
        fprintf(stderr, MAG "\nIssue found, usage of very small or null pointer.\n\n" RESET
               "You seem to have defined a pointer with a value of %p which is either a null pointer or very small, and thus is not a valid memory address.\n",
               si->si_addr);
        err("");
    }
}

void check_stack_overflow_underflow_issue(siginfo_t *si, ucontext_t *ucontext) {
    size_t stack_start;
    size_t stack_end;
    get_original_stack(&stack_start, &stack_end);
    size_t rsp = (size_t) ucontext->uc_mcontext.gregs[REG_RSP];
    // check overflow
    if ((rsp < stack_start && rsp > stack_start - 10) || 
            ((size_t) si->si_addr < stack_start && (size_t) si->si_addr > stack_start - 10)) {
        err(MAG "\nIssue found, stack overflow.\n\n" RESET
               "This problem occurs when you access an address trying to refer to the stack that's actually after the stack in memory. This is often caused by recursion (a function that calls itself) "
               "without a break case.\n\n");
    }
    // check underflow
    if ((rsp < stack_end && rsp > stack_end + 10) || 
            ((size_t) si->si_addr < stack_end && (size_t) si->si_addr > stack_end + 10)) {
        err(MAG "\nIssue found, stack underflow.\n\n" RESET
               "This problem occurs when you access an address trying to refer to the stack that's actually before the stack in memory, or when you try to pop a value without having "
               "pushed anything.\n\n");
    }
}

void check_invalid_rip_issue(siginfo_t *si, ucontext_t *ucontext) {
    void *rip = (void *)ucontext->uc_mcontext.gregs[REG_RIP];
    if (rip < (void*) 10) {
        err(MAG "\nIssue found, tried to jump to an address which is invalid.\n\n" RESET
                "This is likely caused by trying to call an invalid value in an array of function pointers.\n\n");
    }
}

/* A list of all check functions that the general segfault handler iterates through */
typedef void (*segfault_issue_check)(siginfo_t*, ucontext_t*);
segfault_issue_check checks[] = {
    check_readonly_memory_issue,
    check_nullpointer_issue,
    check_stack_overflow_underflow_issue,
    check_invalid_rip_issue,
};

/* calls addr2line to display the line number + file in a source codebase from an address */
void addr2line(void *addr) {
    pid_t pid = fork();
    if (pid) {
        // parent
        int status;
        waitpid(pid, &status, 0);
    } else {
        // child
        char str_addr[10];
        snprintf(str_addr, 10, "%p", addr);
        execvp("addr2line", (char*[]) {"addr2line", "-e", global_info.cmd, str_addr, NULL});
    }
}

/* unwinds the stack and displays the addresses of each call, as well as the source location of it
 * using addr2line internally */
void stack_trace(uint64_t rbp, uint64_t rip) {
    fprintf(stderr, "\nStack Trace (Most recent call first): \n");
    fprintf(stderr, " %p -> ", (void*) rip);
    fflush(stderr);
    addr2line((void*) rip);
    StackFrame *stack = (StackFrame*) rbp;
    while (stack) {
        fprintf(stderr, " %p -> ", (void*) stack->rip);
        fflush(stderr);
        addr2line((void*) stack->rip);
        if (stack->rbp->rip == stack->rip) {
            fprintf(stderr, "Omitted some entries due to repeats likely due to recursion.\n");
            break;
        }
        if ((void*) stack->rbp > global_info.entry_rbp) break;
        stack = stack->rbp;
    }
}

/* general segfault signal handler, tries to check the source of the error for clean error reporting. */ 
void segfault_handler(int sig, siginfo_t *si, void *context) {
    fprintf(stderr, RED "Segmentation fault occurred.\n" RESET);
    ucontext_t *ucontext = (ucontext_t *)context;
    stack_trace((uint64_t) ucontext->uc_mcontext.gregs[REG_RBP], (uint64_t) ucontext->uc_mcontext.gregs[REG_RIP]);
    // Find the issue source
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) checks[i](si, ucontext);
    // Checked all known sources of issues, return a last-resort error
    err(MAG "\nIssue could not be found, segfix cannot handle this cause of segmentation faults.\n" RESET);
}

/* sets the new segfault signal handler and other initiation,
 * called by SEGFIX_INIT macro which should be called at start of the program. */
int segfix_init(char *cmd) {
    // make sure that segfix hasn't already been initiated
    if (global_info.is_initiated) {
        fprintf(stderr, "segfix has already been initiated, cannot initiate twice. Only call SEGFIX_INIT() macro at the start of your main() function.\n");
        return 1;
    }
    global_info.is_initiated = 1;
    /* make sure that the program isn't position independent (this will make debug symbols not be able to find the
     * line number) */
    if (!getauxval(AT_BASE)) {
        fprintf(stderr, "Position independent executables cannot be used in debug programs with segfix. Please use the -no-pie compilation option.\n");
        return 1;
    }
    // save some information about the program
    global_info.cmd = cmd;
    ucontext_t ctx;
    getcontext(&ctx);
    global_info.entry_rbp = (void*) ctx.uc_mcontext.gregs[REG_RBP];
    // set the segfault handler with it's own stack
    struct sigaction sa;
    stack_t ss;
    ss.ss_sp = (void*) ((size_t) malloc(ALT_STACK_SIZE) + ALT_STACK_SIZE);
    if (!ss.ss_sp) {
        fprintf(stderr, "Allocating alternate stack failed (segfix_init() in segfix).\n");
        return 1;
    }
    ss.ss_size = ALT_STACK_SIZE;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) < 0) {
        fprintf(stderr, "Failed to set alternate stack for segmentation faults (segfix_init() in segfix).\n");
        return 1;
    }
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = segfault_handler;
    if (sigemptyset(&sa.sa_mask) < 0) {
        fprintf(stderr, "Failed sigemptyset() in segfix_init() in segfix.\n");
        return 1;
    }
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        fprintf(stderr, "Failed to set segfault action to segfix handler (segfix_init() in segfix).\n");
        return 1;
    }
    // initiate a list of read only memory sections
    list_init(&global_info.rosections.list);
    dl_iterate_phdr(callback, NULL);
    return 0;
}
