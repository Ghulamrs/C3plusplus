// sys/stat.h - a file's kind, size and times, as the Microsoft CRT reports them.
//
// Not Microsoft's header, and not a copy of one. **The layout is the one thing
// that must be exact**: stat() writes the struct through a pointer, so a member
// at the wrong offset is a wrong answer or a write past the object. It is the
// UCRT's `struct _stat64i32` - 32-bit size, 64-bit times - 48 bytes, which is
// what a plain `struct stat` means under the UCRT's defaults:
//
//     0 st_dev  4 st_ino  6 st_mode  8 st_nlink  10 st_uid  12 st_gid
//    16 st_rdev  20 st_size  24 st_atime  32 st_mtime  40 st_ctime
//
// **There is no `stat` in the library to call.** The UCRT's own header makes
// stat() an inline function over _stat64i32, which is the exported name, so
// that is what this one is too.
//
// For the Windows target only, as <sys/types.h>.
//
// Added 2026-10-20 for cppparse's PathUtil.cpp: `struct stat st; stat(path, &st)`
// and `st.st_mode & S_IFDIR`.
#ifndef _CXX1_SYS_STAT_H
#define _CXX1_SYS_STAT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _stat64i32 {
    _dev_t st_dev;
    _ino_t st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    _dev_t st_rdev;
    _off_t st_size;
    __time64_t st_atime;
    __time64_t st_mtime;
    __time64_t st_ctime;
};

// The same object under the name a portable program writes.
struct stat {
    _dev_t st_dev;
    _ino_t st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    _dev_t st_rdev;
    _off_t st_size;
    __time64_t st_atime;
    __time64_t st_mtime;
    __time64_t st_ctime;
};

// The file-kind bits of st_mode and the permission bits, the CRT's values.
#define _S_IFMT   0xF000
#define _S_IFDIR  0x4000
#define _S_IFCHR  0x2000
#define _S_IFIFO  0x1000
#define _S_IFREG  0x8000
#define _S_IREAD  0x0100
#define _S_IWRITE 0x0080
#define _S_IEXEC  0x0040
#define S_IFMT   _S_IFMT
#define S_IFDIR  _S_IFDIR
#define S_IFCHR  _S_IFCHR
#define S_IFREG  _S_IFREG
#define S_IREAD  _S_IREAD
#define S_IWRITE _S_IWRITE
#define S_IEXEC  _S_IEXEC

// 0 and the struct filled, or -1 and errno set.
int _stat64i32(const char *path, struct _stat64i32 *buf);
static inline int stat(const char *path, struct stat *buf) {
    return _stat64i32(path, (struct _stat64i32 *)buf);
}

#ifdef __cplusplus
}
#endif

#endif
