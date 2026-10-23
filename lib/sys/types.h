// sys/types.h - the Microsoft CRT's widths for file sizes, devices and times.
//
// Not Microsoft's header, and not a copy of one, for the reason stdio.h gives:
// what a program wants from it is names for integer widths, and those must be
// the CRT's own, because <sys/stat.h> and <io.h> lay out structs the library
// fills - a width that lied would put every later member at the wrong offset.
//
// The POSIX types of the same names differ in width from platform to platform;
// Linux's and macOS's are below, measured, and the C6000 has none.
//
// Added 2026-10-20 for cppparse's PathUtil.cpp.
#ifndef _CXX1_SYS_TYPES_H
#define _CXX1_SYS_TYPES_H

#if defined(_WIN32)
typedef unsigned int _dev_t;     // a drive number
typedef unsigned short _ino_t;   // always 0 on Windows, and still a member
typedef long _off_t;             // four bytes: LLP64
typedef long long __time64_t;    // what the UCRT's 64-bit time functions take

typedef _dev_t dev_t;
typedef _ino_t ino_t;
typedef _off_t off_t;

#elif defined(__APPLE__) || defined(__linux__)
// **POSIX's, for macOS (arm64) and Linux (x86_64), each measured with the platform's own headers** (M1,
// 2026-10-23): the widths that differ are mode_t, dev_t, nlink_t and blksize_t.
typedef int pid_t;
typedef long ssize_t;
typedef long off_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned long long ino_t;
typedef long long blkcnt_t;
typedef long time_t;
#if defined(__APPLE__)
typedef unsigned short mode_t;
typedef int dev_t;
typedef unsigned short nlink_t;
typedef int blksize_t;
#else
typedef unsigned int mode_t;
typedef unsigned long dev_t;
typedef unsigned long nlink_t;
typedef long blksize_t;
#endif

#else
#error "<sys/types.h> is here for x86_64-windows, x86_64-linux and arm64-darwin"
#endif

#endif
