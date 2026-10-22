// sys/types.h - the Microsoft CRT's widths for file sizes, devices and times.
//
// Not Microsoft's header, and not a copy of one, for the reason stdio.h gives:
// what a program wants from it is names for integer widths, and those must be
// the CRT's own, because <sys/stat.h> and <io.h> lay out structs the library
// fills - a width that lied would put every later member at the wrong offset.
//
// For the Windows target only. The POSIX types of the same names differ in
// width from platform to platform, and a program built for Linux or macOS here
// has no header that says which - so that is refused rather than guessed.
//
// Added 2026-10-20 for cppparse's PathUtil.cpp.
#ifndef _CXX1_SYS_TYPES_H
#define _CXX1_SYS_TYPES_H

#ifndef _WIN32
#error "<sys/types.h> is here for the x86_64-windows target only"
#endif

typedef unsigned int _dev_t;     // a drive number
typedef unsigned short _ino_t;   // always 0 on Windows, and still a member
typedef long _off_t;             // four bytes: LLP64
typedef long long __time64_t;    // what the UCRT's 64-bit time functions take

typedef _dev_t dev_t;
typedef _ino_t ino_t;
typedef _off_t off_t;

#endif
