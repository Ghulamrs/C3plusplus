// M6a.2 W2: variadic calls and variadic definitions through the C6000 selector - the last named
// argument and every one after it on the stack, a word each, as cl6x places them.
#include <cstdarg>
#include <cstdio>

int sum(int n, ...);
int pick(const char *f, int h, ...);
int only(int c, ...);
int many(int a, int b, int c, int d, int e, ...);

int sum(int n, ...) {
    va_list ap;
    int s = 0;
    va_start(ap, n);
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int pick(const char *f, int h, ...) {
    va_list ap;
    va_start(ap, h);
    int s = va_arg(ap, int);
    const char *p = va_arg(ap, const char *);
    unsigned u = va_arg(ap, unsigned);
    va_end(ap);
    return s + *p + *f + h + static_cast<int>(u >> 28);
}

int only(int c, ...) {
    va_list ap, aq;
    va_start(ap, c);
    va_copy(aq, ap);
    int a = va_arg(ap, int);
    int b = va_arg(aq, int);
    va_end(aq);
    va_end(ap);
    return c + a + b;
}

int many(int a, int b, int c, int d, int e, ...) {
    va_list ap;
    va_start(ap, e);
    int s = a + 2 * b + 3 * c + 4 * d + 5 * e;
    for (int i = 0; i < 9; i++) s = s * 3 + va_arg(ap, int);
    va_end(ap);
    return s;
}

int caller(int x, const char *p, short h, char c) {
    std::printf("%d %s %d %c\n", x, p, h, c);
    std::printf("none\n");
    int r = sum(3, x, 7, h) + pick("a", h, x, p, 0xf0000000u);
    r += only(c, x);
    r += sum(0);
    return r + many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, x);
}

int main() {
    int r = caller(41, "zed", -3, 'q');
    std::printf("%d %d\n", r, sum(12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));
    for (int i = 0; i < 3; i++) std::printf("%d:%d:%s\n", i, only(i, i * 5), i ? "t" : "f");
    return 0;
}
