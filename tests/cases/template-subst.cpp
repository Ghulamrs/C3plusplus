// M8.1: function templates over type parameters whose bodies need no lookup - made by substitution under
// CXX1_TEMPLATE_SUBST from a replayed sibling of the same layout; `twice` is outside the subset and replays.
extern "C" int printf(const char *, ...);

template <class T> void exchange(T &a, T &b) { T t = a; a = b; b = t; }
template <class T> T pick(T *p) { return *p; }
template <class T, class U> U second(T a, U b) { T c = a; a = c; return b; }
template <class T> T *where(T &a) { return &a; }
template <class T> T twice(T a) { return a + a; }

int main() {
    int i = 1, j = 2;
    unsigned u = 3u, v = 4u;
    float f = 1.5f, g = 2.5f;
    long long x = 5, y = 6;
    unsigned long long p = 7, q = 8;
    double d = 0.25, e = 0.75;
    char c = 'a', k = 'b';
    exchange(i, j);
    exchange(u, v);
    exchange(f, g);
    exchange(x, y);
    exchange(p, q);
    exchange(d, e);
    exchange(c, k);
    printf("%d %d %u %u %g %g %lld %lld %llu %llu %g %g %c %c\n", i, j, u, v, f, g, x, y, p, q, d, e, c, k);
    printf("%d %u %g %lld\n", pick(&i), pick(&u), pick(&f), pick(&x));
    printf("%u %d %g\n", second(i, u), second(u, i), second(f, d));
    printf("%d %d\n", *where(i), where(u) == &u);
    printf("%d %g\n", twice(i), twice(d));
    return 0;
}
