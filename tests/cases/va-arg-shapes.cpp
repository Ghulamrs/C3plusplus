// A variadic function under -front=ssa (M3b.2 round 22): va_arg of each type the walker takes - int, unsigned,
// long long, double, a pointer - named arguments of both kinds in front of the `...`, more arguments than
// registers so the overflow area is read, va_copy, a list walked by a callee through `*p`, and a variadic
// member function, static and not. Every call is a statement of its own. clang prints the same lines.
#include <stdarg.h>
extern "C" int printf(const char *, ...);

static long long ints(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long long sum = 0;
    for (int i = 0; i < n; i++) sum += va_arg(ap, int);
    va_end(ap);
    return sum;
}

static double mix(int a, double b, int c, ...) {
    va_list ap;
    va_start(ap, c);
    double total = a + b + c;
    total += va_arg(ap, double);
    total += va_arg(ap, int);
    total += (double)va_arg(ap, long long);
    total += va_arg(ap, unsigned);
    total += va_arg(ap, double);
    va_end(ap);
    return total;
}

// Eight ints and ten doubles: the last two of each come off the stack on every ABI.
static double many(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double total = 0;
    for (int i = 0; i < n; i++) {
        total += va_arg(ap, int);
        total += va_arg(ap, double);
    }
    va_end(ap);
    return total;
}

static int texts(const char *first, ...) {
    va_list ap;
    va_start(ap, first);
    int chars = 0;
    for (const char *s = first; s != 0; s = va_arg(ap, const char *))
        while (*s++) chars++;
    void *p = va_arg(ap, void *);
    va_end(ap);
    return chars * 10 + (p == 0 ? 1 : 2);
}

static long long walked(int n, va_list *p) {
    long long sum = 0;
    for (int i = 0; i < n; i++) sum += va_arg(*p, long long);
    return sum;
}

static long long twice(int n, ...) {
    va_list ap, again;
    va_start(ap, n);
    va_copy(again, ap);
    long long first = walked(n, &ap);
    long long second = 0;
    for (int i = 0; i < n; i++) second += 3 * va_arg(again, long long);
    va_end(again);
    va_end(ap);
    return first * 1000 + second;
}

struct Tally {
    int base;
    explicit Tally(int b) : base(b) {}
    int add(int n, ...) {
        va_list ap;
        va_start(ap, n);
        int sum = base;
        for (int i = 0; i < n; i++) sum += va_arg(ap, int);
        va_end(ap);
        return sum;
    }
    static double scale(double k, int n, ...) {
        va_list ap;
        va_start(ap, n);
        double sum = 0;
        for (int i = 0; i < n; i++) sum += k * va_arg(ap, double);
        va_end(ap);
        return sum;
    }
};

int main() {
    long long a = ints(5, 1, 2, 3, 4, 5);
    printf("%lld\n", a);
    long long b = ints(0);
    printf("%lld\n", b);
    double c = mix(1, 2.5, 3, 4.25, 5, 6000000000LL, 7u, 8.5);
    printf("%.2f\n", c);
    double d = many(10, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8, 8.5, 9, 9.5, 10, 10.5);
    printf("%.1f\n", d);
    int e = texts("ab", "cde", "", "fghi", (const char *)0, (void *)0);
    printf("%d\n", e);
    int f = texts("x", (const char *)0, (void *)&e);
    printf("%d\n", f);
    long long g = twice(3, 10000000000LL, 20LL, -3LL);
    printf("%lld\n", g);
    Tally t(100);
    int h = t.add(3, 1, 2, 3);
    printf("%d\n", h);
    double k = Tally::scale(0.5, 4, 1.0, 2.0, 3.0, 4.0);
    printf("%.2f\n", k);
    return 0;
}
