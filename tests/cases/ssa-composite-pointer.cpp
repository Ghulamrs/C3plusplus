// A conditional of two pointers whose pointees differ - in cv, one a pointer to void, or a class and its base - takes
// the composite pointer type, [expr.cond]/6 with [expr]/13, each arm converted to it (M3b.2, the SSA lowering's `?:`).
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; };
struct D : A, B { int d; };

static const int *pick(bool c, int *x, const int *y) { return c ? x : y; }
static const void *anything(bool c, const char *s, void *v) { return c ? s : v; }
static B *side(bool c, D *d, B *b) { return c ? d : b; }
static const B *sideConst(bool c, const D *d, B *b) { return c ? d : b; }
static int read(bool c, int *x, const int *y) { return *(c ? x : y); }

int main() {
    int i = 4;
    const int j = 9;
    printf("%d %d\n", *pick(true, &i, &j), *pick(false, &i, &j));
    char s[] = "hi";
    int k = 7;
    printf("%d %d\n", anything(true, s, &k) == (const void *)s, anything(false, s, &k) == (const void *)&k);
    D d;
    d.a = 1; d.b = 2; d.d = 3;
    B b;
    b.b = 5;
    printf("%d %d\n", side(true, &d, &b)->b, side(false, &d, &b)->b);
    printf("%d %d\n", sideConst(true, &d, &b)->b, sideConst(false, &d, &b)->b);
    D *none = 0;
    printf("%d\n", side(true, none, &b) == 0);
    printf("%d %d\n", read(true, &i, &j), read(false, &i, &j));
    return 0;
}
