// M8.1-early: function templates over type parameters whose bodies need no lookup - made by substitution under
// -templates=subst from a replayed sibling of the same layout: every arithmetic size, `*`, `&`, declarations, `=`, return.
extern "C" int printf(const char *, ...);

template <class T> void exchange(T &a, T &b) { T t = a; a = b; b = t; }
template <class T> T pick(T *p) { return *p; }
template <class T, class U> U second(T a, U b) { T c = a; a = c; return b; }
template <class T> T *where(T &a) { return &a; }
template <class T> void put(T *p, T a) { T *q = p; *q = a; }
template <class T> T through(T a) { T *p = &a; T **pp = &p; T b = **pp; const T c = b; return c; }
template <class T> T twice(T a) { return a + a; }

int main() {
    int i = 1, j = 2;
    unsigned u = 3u, v = 4u;
    float f = 1.5f, g = 2.5f;
    long long x = 5, y = 6;
    unsigned long long p = 7, q = 8;
    double d = 0.25, e = 0.75;
    char c = 'a', k = 'b';
    signed char sc = -3, sd = 9;
    unsigned char uc = 200, ud = 7;
    short s = -300, t = 12;
    unsigned short us = 60000, ut = 5;
    exchange(i, j);
    exchange(u, v);
    exchange(f, g);
    exchange(x, y);
    exchange(p, q);
    exchange(d, e);
    exchange(c, k);
    exchange(sc, sd);
    exchange(uc, ud);
    exchange(s, t);
    exchange(us, ut);
    printf("%d %d %u %u %g %g %lld %lld %llu %llu %g %g %c %c\n", i, j, u, v, f, g, x, y, p, q, d, e, c, k);
    printf("%d %d %d %d %d %d %d %d\n", sc, sd, uc, ud, s, t, us, ut);
    printf("%d %u %g %lld %d %d %d %d\n", pick(&i), pick(&u), pick(&f), pick(&x), pick(&sc), pick(&uc), pick(&s), pick(&us));
    printf("%u %d %g\n", second(i, u), second(u, i), second(f, d));
    printf("%d %d %d %d\n", *where(i), where(u) == &u, *where(s), where(us) == &us);
    put(&i, -7); put(&u, 4000000000u); put(&s, (short)-2); put(&us, (unsigned short)65535); put(&sc, (signed char)-128); put(&uc, (unsigned char)255);
    printf("%d %u %d %d %d %d\n", i, u, s, us, sc, uc);
    printf("%d %u %lld %llu %g %g %d %d\n", through(i), through(u), through(x), through(p), through(f), through(d), through(s), through(us));
    printf("%d %g\n", twice(i), twice(d));
    return 0;
}
