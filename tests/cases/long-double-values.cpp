// A long double as a value: on x86_64-linux an x87 value, which the SSA path keeps in a 16-byte slot and moves through
// st0. Constants, the four operators and every comparison, conversions both ways, steps, arms that merge, parameters
// and returns, members, an array, a list, a static, a temporary bound, `new` and a catch - each exact in a double too.
#include <initializer_list>
extern "C" int printf(const char *, ...);

struct Pair { long double a; int n; long double b; };

static long double twice(long double x) { return x + x; }
static long double mix(int i, long double x, double d, long double y) { return x * i - y / d; }
static Pair make(long double a, long double b) { Pair p = {a, 3, b}; return p; }
static long double sum(Pair p) { return p.a + p.b + p.n; }
static long double pick(bool c, long double x, long double y) { return c ? x : y; }
static int cmp(long double x, long double y) {
    return (x < y) + (x <= y) * 2 + (x > y) * 4 + (x >= y) * 8 + (x == y) * 16 + (x != y) * 32;
}
struct Total {
    long double s;
    Total(std::initializer_list<long double> l) : s(0) {
        for (const long double *p = l.begin(); p != l.end(); ++p) s += *p;
    }
};
static long double counter() { static long double n = 1.5L; n += 1; return n; }
static long double viaRef(const long double &r) { return r * 2; }
static long double fallsOff(int k) { if (k > 0) return k; }

int main() {
    long double x = 1.5L, y = -2.25L;
    printf("%Lg %Lg\n", twice(x), -y);
    printf("%Lg\n", mix(3, x, 0.5, y));
    Pair p = make(0.25L, 0.5L);
    printf("%Lg %d\n", sum(p), p.n);
    printf("%Lg %Lg\n", pick(true, x, y), pick(false, x, y));
    printf("%d %d %d\n", cmp(x, y), cmp(y, x), cmp(x, x));
    long double a[5] = {1, 2.5L, -3};
    long double s = 0;
    for (long double v : a) s += v;
    printf("%Lg %Lg\n", s, a[3]);
    ++x;
    x--;
    x++;
    printf("%Lg\n", x);
    const int i = (int)(y * 4);
    const unsigned long long u = (unsigned long long)(x * 1000);
    const float f = (float)y;
    const double d = y;
    printf("%d %llu %g %g\n", i, u, (double)f, d);
    const long double from = (long double)u + (long double)i + (long double)f + 7u;
    printf("%Lg\n", from);
    if (y) printf("y is true\n");
    const bool b = !x;
    printf("%d\n", b);
    const Total t = {1, 2, 3.5L};
    printf("%Lg\n", t.s);
    counter();
    printf("%Lg\n", counter());
    printf("%Lg\n", viaRef(x + 1));
    long double *q = new long double(6.5L);
    *q *= 2;
    *q -= 0.5L;
    *q /= 5;
    printf("%Lg\n", *q);
    delete q;
    const long double z = x > 0 && y < 0 ? x : y;
    printf("%Lg %Lg\n", z, fallsOff(4));
    try { throw 4.75L; } catch (long double v) { printf("caught %Lg\n", v); }
    return 0;
}
