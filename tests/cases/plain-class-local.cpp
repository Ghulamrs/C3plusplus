// A class with no constructor, destructor or copy of its own is its bytes: default-initialised it is left alone,
// `{}` zeroes it, a braced list stores members in order and zeroes the rest, `= o` and `(o)` copy, `a = b` copies
// (M3b.2: what the SSA lowering does for such an object, held against clang).
extern "C" int printf(const char *, ...);

struct S { int x; int y; };
struct W { char c; double d; long long k; unsigned short s; };

int f(S *po) {
    S a;
    a.x = 5; a.y = 6;
    S b = {};
    S c{};
    S d = {1};
    S e = *po;
    S g(*po);
    g = a;
    return a.x + b.x + c.y + d.x + d.y + e.y + g.x * 10;
}

double w(const W *pw) {
    W m = {'a', 2.5, 7, 3};
    W n = *pw;
    W z{};
    n = m;
    m.k = 40;
    return n.c + n.d + static_cast<double>(n.k) + n.s + z.d + static_cast<double>(m.k) * 100;
}

int main() {
    S o = {3, 4};
    W q = {'b', 1.5, 9, 2};
    printf("%d %g\n", f(&o), w(&q));
    return 0;
}
