// `return {};` of a scalar, a pointer, a double and a plain struct: [dcl.init.list]/3 value-initialises each, which is
// its zero; the lowering refused all four as "a return with no value" (R1, round 7).
extern "C" int printf(const char *, ...);
struct P { int a; double b; char c[3]; };
int i(int k) { if (k) return k; return {}; }
int *p(int *q) { if (q) return q; return {}; }
double d(int k) { if (k) return 1.5; return {}; }
P s(int k) { if (k) { P r = { k, 2.5, { 'x', 'y', 0 } }; return r; } return {}; }
int main() {
    int x = 7;
    const P a = s(0), b = s(3);
    printf("%d %d %d %d %g %g\n", i(0), i(4), p(0) == 0, p(&x) == &x, d(0), d(1));
    printf("%d %g %d | %d %g %s\n", a.a, a.b, a.c[0], b.a, b.b, b.c);
    return 0;
}
