// io.h - the Microsoft CRT's low-level I/O, as much of it as a program here
// has asked for.
//
// Not Microsoft's header, and not a copy of one, for the reason stdio.h gives
// at length: what a program wants from it is the prototypes, and a prototype
// is ordinary C. These must agree with the CRT's, because the program links
// against libcmt - a declaration that lied would fail at the link or, worse,
// pass the wrong bytes.
//
// This exists for `_setmode(_fileno(stdout), _O_BINARY)`, which is how a
// Windows program stops the CRT translating '\n' into "\r\n" on the way out.
// A compiler whose output is compared byte for byte against a recorded file
// has to do that, and there is no portable spelling of it.
//
// It is a Windows header and nothing else declares these, so a program that
// includes it and is built for another target links against symbols that
// platform has never had - which is the honest failure and the same one a
// real toolchain gives.
#ifndef _CXX1_IO_H
#define _CXX1_IO_H

#include <stdint.h>
#ifdef _WIN32
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Sets the translation mode of an already-open file descriptor and answers
// the previous one, or -1. The mode is one of the _O_ values in <fcntl.h>.
int _setmode(int fd, int mode);

// 0 when the file is there with the access asked (0 there, 2 write, 4 read,
// 6 both), -1 and errno otherwise.
int _access(const char *path, int mode);

// **Walking a directory** (2026-10-20, for cppparse's PathUtil.cpp):
// _findfirst on "dir/*" answers a handle and the first entry, _findnext the
// rest until it answers -1, _findclose lets the handle go. The struct is the
// UCRT's `_finddata64i32_t` - 64-bit times, a 32-bit size, 296 bytes - which is
// what `_finddata_t` and the plain names mean under its defaults, and like
// stat() the plain names are not in the library: the UCRT's header renames
// them, so this one does too.
//
//     0 attrib  8 time_create  16 time_access  24 time_write  32 size  36 name
#ifdef _WIN32
typedef unsigned long _fsize_t;
struct _finddata64i32_t {
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    _fsize_t size;
    char name[260];
};
#define _finddata_t _finddata64i32_t
#define _findfirst _findfirst64i32
#define _findnext _findnext64i32

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

intptr_t _findfirst64i32(const char *spec, struct _finddata64i32_t *found);
int _findnext64i32(intptr_t handle, struct _finddata64i32_t *found);
int _findclose(intptr_t handle);
#endif

#ifdef __cplusplus
}
#endif

#endif
