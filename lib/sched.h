// sched.h - yielding the processor, and on Linux the CPUs a process may run on.
//
// cpu_set_t is glibc's: 1024 bits, 128 bytes - measured (M1, 2026-10-23). macOS has no affinity call;
// it gets sched_yield alone, as its own header does.
#ifndef _CXX1_SCHED_H
#define _CXX1_SCHED_H

#if defined(_WIN32) || defined(__TMS320C6X__)
#error "<sched.h> is POSIX's - x86_64-linux and arm64-darwin"
#endif

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int sched_yield(void);

#if defined(__linux__)
#define CPU_SETSIZE 1024
typedef struct { unsigned long __bits[CPU_SETSIZE / (8 * sizeof(unsigned long))]; } cpu_set_t;
#define __CXX1_CPU_WORD(c) ((c) / (8 * sizeof(unsigned long)))
#define __CXX1_CPU_BIT(c)  (1UL << ((c) % (8 * sizeof(unsigned long))))
#define CPU_ZERO(set) \
    do { for (size_t __i = 0; __i < CPU_SETSIZE / (8 * sizeof(unsigned long)); __i++) (set)->__bits[__i] = 0; } while (0)
#define CPU_SET(c, set)   ((set)->__bits[__CXX1_CPU_WORD(c)] |= __CXX1_CPU_BIT(c))
#define CPU_CLR(c, set)   ((set)->__bits[__CXX1_CPU_WORD(c)] &= ~__CXX1_CPU_BIT(c))
#define CPU_ISSET(c, set) (((set)->__bits[__CXX1_CPU_WORD(c)] & __CXX1_CPU_BIT(c)) != 0)
int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set);
#endif

#ifdef __cplusplus
}
#endif

#endif
