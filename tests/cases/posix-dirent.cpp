// lib/dirent.h, which cppparse's PathUtil.cpp lists a directory with: struct dirent has the platform's layout,
// measured against the real headers, and opendir/readdir/closedir walk a directory this makes.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
static_assert(sizeof(struct dirent) == 1048 && offsetof(struct dirent, d_name) == 21 &&
              offsetof(struct dirent, d_type) == 20, "macOS's struct dirent");
#else
static_assert(sizeof(struct dirent) == 280 && offsetof(struct dirent, d_name) == 19 &&
              offsetof(struct dirent, d_type) == 18, "glibc's struct dirent");
#endif
int main() {
    char dir[64], file[96];
    std::snprintf(dir, sizeof dir, "/tmp/cxx3-dirent-%d", (int)getpid());
    int made = mkdir(dir, 0700) == 0;
    for (int i = 0; i < 3; i++) {
        std::snprintf(file, sizeof file, "%s/f%d.txt", dir, i);
        if (FILE *f = std::fopen(file, "w")) { std::fputs("abc", f); std::fclose(f); }
    }
    std::snprintf(file, sizeof file, "%s/sub", dir);
    mkdir(file, 0700);
    int files = 0, dirs = 0, dots = 0;
    DIR *d = opendir(dir);
    for (struct dirent *e = d ? readdir(d) : nullptr; e; e = readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) dots++;
        else if (e->d_type == DT_DIR) dirs++;
        else if (e->d_type == DT_REG && std::strncmp(e->d_name, "f", 1) == 0) files++;
    }
    int closed = d ? closedir(d) : -1;
    for (int i = 0; i < 3; i++) { std::snprintf(file, sizeof file, "%s/f%d.txt", dir, i); unlink(file); }
    std::snprintf(file, sizeof file, "%s/sub", dir);
    rmdir(file);
    std::printf("made %d files %d dirs %d dots %d closed %d removed %d missing %d\n", made, files, dirs, dots, closed,
                rmdir(dir) == 0, opendir(dir) == nullptr);
    return 0;
}
