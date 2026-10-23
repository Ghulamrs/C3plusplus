// lib/'s POSIX headers, which the compiler's own Driver.cpp, main.cpp and Preprocessor.cpp need on Linux
// and macOS: each struct the library writes through a pointer has the platform's own layout - asserted
// here as measured with clang and gcc against the real headers - and each call does what it says.
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glob.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
static_assert(sizeof(struct stat) == 144 && offsetof(struct stat, st_mode) == 4 &&
              offsetof(struct stat, st_size) == 96, "macOS's struct stat");
static_assert(sizeof(glob_t) == 88 && offsetof(glob_t, gl_pathv) == 32, "macOS's glob_t");
static_assert(sizeof(mode_t) == 2 && sizeof(posix_spawnattr_t) == 8, "macOS's widths");
static char **environment() { return *_NSGetEnviron(); }
#else
#include <sched.h>
static_assert(sizeof(struct stat) == 144 && offsetof(struct stat, st_mode) == 24 &&
              offsetof(struct stat, st_size) == 48, "glibc's struct stat");
static_assert(sizeof(glob_t) == 72 && offsetof(glob_t, gl_pathv) == 8, "glibc's glob_t");
static_assert(sizeof(mode_t) == 4 && sizeof(posix_spawnattr_t) == 336 && sizeof(cpu_set_t) == 128,
              "glibc's widths");
extern char **environ;
static char **environment() { return environ; }
#endif
int main() {
    char dir[64];
    std::snprintf(dir, sizeof dir, "/tmp/cxx3-posix-%d", (int)getpid());
    int made = mkdir(dir, 0700) == 0;
    struct stat st;
    int isDir = stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
    char file[96];
    for (int i = 0; i < 3; i++) {
        std::snprintf(file, sizeof file, "%s/f%d.txt", dir, i);
        if (FILE *f = std::fopen(file, "w")) { std::fputs("abc", f); std::fclose(f); }
    }
    std::snprintf(file, sizeof file, "%s/f1.txt", dir);
    int size = stat(file, &st) == 0 && S_ISREG(st.st_mode) ? (int)st.st_size : -1;
    char pattern[96];
    std::snprintf(pattern, sizeof pattern, "%s/f*.txt", dir);
    glob_t g;
    int globbed = glob(pattern, 0, nullptr, &g) == 0 ? (int)g.gl_pathc : -1;
    const char *last = globbed > 0 ? std::strrchr(g.gl_pathv[g.gl_pathc - 1], '/') + 1 : "";
    std::printf("made %d dir %d size %d globbed %d last %s\n", made, isDir, size, globbed, last);
    globfree(&g);
    const char *argv[] = { "/bin/sh", "-c", "exit 3", nullptr };
    pid_t pid = 0;
    int spawned = posix_spawn(&pid, "/bin/sh", nullptr, nullptr, const_cast<char *const *>(argv), environment());
    int status = 0;
    waitpid(pid, &status, 0);
    std::printf("spawned %d exited %d code %d\n", spawned, WIFEXITED(status) ? 1 : 0, WEXITSTATUS(status));
    char here[PATH_MAX], real[PATH_MAX];
    int cwd = getcwd(here, sizeof here) != nullptr && realpath(here, real) != nullptr && real[0] == '/';
    for (int i = 0; i < 3; i++) { std::snprintf(file, sizeof file, "%s/f%d.txt", dir, i); unlink(file); }
    std::printf("cwd %d removed %d\n", cwd, rmdir(dir) == 0);
    return 0;
}
