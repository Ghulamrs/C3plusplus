// M6a.2 W8: the C6000 selector's last reasons but R1's - a 64-bit argument and parameter past the sixth (a pair in
// A11:A10 .. B13:B12) with values live across the call, an array read that decays (std::forward of an array reference),
// a member function's variadic call, and a 64-bit divide by a constant (TI's helpers, as cl6x calls them).
#include <stdarg.h>
#include <stdio.h>

template <class T> struct Id { typedef T type; };
template <class T> T &&fwd(typename Id<T>::type &t) { return static_cast<T &&>(t); }

static int length(const char *s) { int n = 0; while (s[n] != 0) n++; return n; }
template <class T> int decayed(T &&a) { return length(fwd<T>(a)); }

long long seventh(int a, int b, int c, int d, int e, int f, long long g, int h) {
    return a + b + c + d + e + f + g * 3 + h;
}
long long tenth(int a, int b, int c, int d, int e, int f, int g, int h, int i, long long j) {
    return a + b + c + d + e + f + g + h + i + j;
}
long long eighth(long long a, int b, int c, int d, int e, int f, int g, long long h) {
    return a - h + b + c + d + e + f + g;
}

struct Sum {
    int base;
    explicit Sum(int b);
    int add(int n, ...);
};
Sum::Sum(int b) : base(b) {}
int Sum::add(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = base;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

long long quot(long long x) { return x / 10; }
unsigned long long rem(unsigned long long x) { return x % 7; }
long long negQuot(long long x) { return x / -1000003; }

long long (*through[])(int, int, int, int, int, int, long long, int) = { seventh };
long long (*divide[])(long long) = { quot };

int main() {
    long long kept = 5000000000LL;
    int k = 11;
    long long r1 = seventh(1, 2, 3, 4, 5, 6, kept, k);
    long long r2 = tenth(1, 2, 3, 4, 5, 6, 7, 8, 9, kept + 1);
    long long r3 = eighth(kept, k, 2, 3, 4, 5, 6, -kept);
    long long r4 = through[k - 11](6, 5, 4, 3, 2, 1, -kept, k);
    printf("%lld %lld %lld %lld %lld %d %lld\n", r1, r2, r3, r4, kept, k, divide[k - 11](kept));
    const char text[6] = "hello";
    printf("%d %d\n", decayed(text), decayed("four"));
    Sum s(100);
    printf("%d %d\n", s.add(3, 1, 2, 3), s.add(0));
    printf("%lld %lld %llu %llu %lld\n", quot(-123456789012LL), quot(99), rem(18446744073709551615ULL), rem(13), negQuot(9000000000000LL));
    return 0;
}
