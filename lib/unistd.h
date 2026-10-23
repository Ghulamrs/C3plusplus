// unistd.h - POSIX's process, directory and descriptor calls, for macOS (arm64) and Linux (x86_64).
//
// Not the platforms' headers and not copies of them: the declarations a program here has asked for -
// the compiler's own Driver.cpp first (M1, 2026-10-23) - with every constant measured from each
// platform's own header. Windows has <direct.h> and <io.h> instead, and the C6000 has neither.
#ifndef _CXX1_UNISTD_H
#define _CXX1_UNISTD_H

#if defined(_WIN32) || defined(__TMS320C6X__)
#error "<unistd.h> is POSIX's - x86_64-linux and arm64-darwin"
#endif

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

// sysconf's names that differ: 58 on macOS, 84 under glibc - measured.
#if defined(__APPLE__)
#define _SC_NPROCESSORS_ONLN 58
#else
#define _SC_NPROCESSORS_ONLN 84
#endif

pid_t getpid(void);
pid_t getppid(void);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int rmdir(const char *path);
int unlink(const char *path);
int access(const char *path, int mode);
ssize_t readlink(const char *path, char *buf, size_t size);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int close(int fd);
int isatty(int fd);
int pipe(int fds[2]);
int dup2(int from, int to);
long sysconf(int name);
unsigned int sleep(unsigned int seconds);

#ifdef __cplusplus
}
#endif

#endif
