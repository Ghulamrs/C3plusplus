// spawn.h - POSIX's posix_spawn, for macOS (arm64) and Linux (x86_64).
//
// The two attribute types are pointers on macOS and structs under glibc, 336 and 80 bytes - measured
// (M1, 2026-10-23). A program here that only passes null for them needs nothing more than their size,
// which is what is kept: opaque, aligned as the real ones are.
#ifndef _CXX1_SPAWN_H
#define _CXX1_SPAWN_H

#if defined(_WIN32) || defined(__TMS320C6X__)
#error "<spawn.h> is POSIX's - x86_64-linux and arm64-darwin"
#endif

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__APPLE__)
typedef void *posix_spawnattr_t;
typedef void *posix_spawn_file_actions_t;
#else
typedef struct { long __opaque[42]; } posix_spawnattr_t;           // 336 bytes
typedef struct { long __opaque[10]; } posix_spawn_file_actions_t;  // 80 bytes
#endif

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
                const posix_spawnattr_t *attributes, char *const argv[], char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *actions,
                 const posix_spawnattr_t *attributes, char *const argv[], char *const envp[]);

#ifdef __cplusplus
}
#endif

#endif
