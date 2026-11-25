// dirent.h - POSIX's directory reading, for macOS (arm64) and Linux (x86_64).
//
// readdir() returns a pointer into the library's own buffer, so the layout is the one thing that has to be
// exact: glibc's is 280 bytes with d_name at 19; macOS's (64-bit inodes, arm64's only kind, so no $INODE64
// name) is 1048 with d_name at 21. DIR is opaque on both. Added 2026-11-25 for cppparse's PathUtil.cpp.
#ifndef _CXX1_DIRENT_H
#define _CXX1_DIRENT_H

#if defined(_WIN32) || defined(__TMS320C6X__)
#error "<dirent.h> is POSIX's - x86_64-linux and arm64-darwin"
#endif

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __cxx1_dir DIR;

#if defined(__APPLE__)
struct dirent {
    unsigned long long d_ino;      //  0
    unsigned long long d_seekoff;  //  8
    unsigned short d_reclen;       // 16
    unsigned short d_namlen;       // 18
    unsigned char d_type;          // 20
    char d_name[1024];             // 21, to 1048
};
#else
struct dirent {
    unsigned long long d_ino;      //  0
    long long d_off;               //  8
    unsigned short d_reclen;       // 16
    unsigned char d_type;          // 18
    char d_name[256];              // 19, to 280
};
#endif

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
#define DT_LNK     10

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
