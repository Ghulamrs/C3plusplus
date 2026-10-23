// mach-o/dyld.h - where the running program is, on macOS: the one call of Apple's header that the
// compiler's own Driver.cpp makes (M1, 2026-10-23). 0 and the path, or -1 and the size wanted.
#ifndef _CXX1_MACH_O_DYLD_H
#define _CXX1_MACH_O_DYLD_H

#if !defined(__APPLE__)
#error "<mach-o/dyld.h> is macOS's"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int _NSGetExecutablePath(char *buf, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
