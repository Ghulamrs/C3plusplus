// sys/wait.h - POSIX's waitpid and the status it reports, for macOS (arm64) and Linux (x86_64).
//
// The status encoding is the same on both - the exit code in bits 8-15, a terminating signal in the
// low seven - and so are WNOHANG's 1 (M1, 2026-10-23, measured on both).
#ifndef _CXX1_SYS_WAIT_H
#define _CXX1_SYS_WAIT_H

#if defined(_WIN32) || defined(__TMS320C6X__)
#error "<sys/wait.h> is POSIX's - x86_64-linux and arm64-darwin"
#endif

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WNOHANG 1
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFEXITED(s)   (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (WTERMSIG(s) != 0 && WTERMSIG(s) != 0x7f)

pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);

#ifdef __cplusplus
}
#endif

#endif
