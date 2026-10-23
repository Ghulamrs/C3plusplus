// crt_externs.h - the environment on macOS, where an executable reaches `environ` through an accessor
// rather than a symbol: the compiler's own Driver.cpp hands it to posix_spawn (M1, 2026-10-23).
#ifndef _CXX1_CRT_EXTERNS_H
#define _CXX1_CRT_EXTERNS_H

#if !defined(__APPLE__)
#error "<crt_externs.h> is macOS's"
#endif

#ifdef __cplusplus
extern "C" {
#endif

char ***_NSGetEnviron(void);

#ifdef __cplusplus
}
#endif

#endif
