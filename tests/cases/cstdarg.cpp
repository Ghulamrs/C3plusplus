// <cstdarg> (L18): `std::va_list` and the four macros, va_copy included ([cstdarg.syn] takes C99's).
// A list walked twice - once through a copy - and handed on to vsnprintf, which reads the
// platform's va_list. clang prints the same lines.
#include <cstdarg>
#include <cstdio>

static int sum(int count, ...) {
    std::va_list ap;
    va_start(ap, count);
    std::va_list again;
    va_copy(again, ap);
    int total = 0;
    for (int i = 0; i < count; i++) total += va_arg(ap, int);
    int twice = 0;
    for (int i = 0; i < count; i++) twice += 2 * va_arg(again, int);
    va_end(again);
    va_end(ap);
    return total * 1000 + twice;
}

static void say(const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    char buf[64];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("[%s]\n", buf);
}

int main() {
    std::printf("%d\n", sum(4, 1, 2, 3, 4));
    say("%d-%s-%.1f", 7, "x", 2.5);
    return 0;
}
