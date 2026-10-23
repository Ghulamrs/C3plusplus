// glob.h - POSIX's pattern expansion, for macOS (arm64) and Linux (x86_64).
//
// glob() fills the struct through a pointer, so its layout is the one thing that has to be exact, and
// the two platforms agree only on where gl_pathc is: gl_pathv is at 8 under glibc and at 32 on macOS,
// and the struct is 72 bytes and 88. Each measured with offsetof against the platform's own header
// (M1, 2026-10-23); the flags differ too. The function pointers past gl_flags are the alternate
// directory functions, never set here, and kept only for their room.
#ifndef _CXX1_GLOB_H
#define _CXX1_GLOB_H

#if defined(_WIN32) || defined(__TMS320C6X__)
#error "<glob.h> is POSIX's - x86_64-linux and arm64-darwin"
#endif

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__APPLE__)
typedef struct {
    size_t gl_pathc;          //  0
    int gl_matchc;            //  8
    size_t gl_offs;           // 16
    int gl_flags;             // 24
    char **gl_pathv;          // 32
    void *gl_errfunc;
    void *gl_closedir, *gl_readdir, *gl_opendir, *gl_lstat, *gl_stat;   // to 88
} glob_t;
#define GLOB_ERR      0x0004
#define GLOB_MARK     0x0008
#define GLOB_NOCHECK  0x0010
#define GLOB_NOSORT   0x0020
#define GLOB_NOESCAPE 0x2000
#define GLOB_NOSPACE  (-1)
#define GLOB_ABORTED  (-2)
#define GLOB_NOMATCH  (-3)
#else
typedef struct {
    size_t gl_pathc;          //  0
    char **gl_pathv;          //  8
    size_t gl_offs;           // 16
    int gl_flags;             // 24
    void *gl_closedir, *gl_readdir, *gl_opendir, *gl_lstat, *gl_stat;   // to 72
} glob_t;
#define GLOB_ERR      (1 << 0)
#define GLOB_MARK     (1 << 1)
#define GLOB_NOSORT   (1 << 2)
#define GLOB_NOCHECK  (1 << 4)
#define GLOB_NOESCAPE (1 << 6)
#define GLOB_NOSPACE  1
#define GLOB_ABORTED  2
#define GLOB_NOMATCH  3
#endif

int glob(const char *pattern, int flags, int (*errfunc)(const char *path, int error), glob_t *found);
void globfree(glob_t *found);

#ifdef __cplusplus
}
#endif

#endif
