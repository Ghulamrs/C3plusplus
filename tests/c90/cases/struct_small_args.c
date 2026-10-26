// expect: 23
// A struct of 8 bytes or less goes to a function by value, in registers on
// the targets that carry one (A5:A4 on the C6000); larger ones by copy. Every
// size, beside scalars, in pairs, past the register file, through a pointer.
#include <stdio.h>
#include <stdarg.h>
struct S1 { char a; }; struct S3 { char a, b, c; }; struct S4 { int a; }; struct S6 { short a, b, c; };
struct S8 { int a, b; }; struct D { double d; }; struct S12 { int a, b, c; };
static int u1(struct S1 s) { return s.a; }
static int u3(struct S3 s, int k) { return s.a * 100 + s.b * 10 + s.c + k; }
static int u6(int k, struct S6 s) { return s.a + s.b + s.c + k; }
static int u8(struct S8 s, struct S8 t) { return s.a * t.b - s.b * t.a; }
static double ud(struct D d, double x) { return d.d + x; }
static int u12(struct S12 s, struct S3 t) { return s.a + s.b + s.c + t.c; }
static int many(int a, int b, int c, int d, int e, int f, int g, int h, int i, struct S8 s, struct S3 t, struct S6 u) {
    return a + b + c + d + e + f + g + h + i + s.a + s.b + t.c + u.c;
}
static int va(int n, ...) { va_list ap; int sum = 0; va_start(ap, n); sum = va_arg(ap, int) + va_arg(ap, int); va_end(ap); return sum + n; }
static struct S8 mk8(int a, int b) { struct S8 s; s.a = a; s.b = b; return s; }
static struct S3 mk3(void) { struct S3 s = {1, 2, 3}; return s; }
static int (*fp)(struct S8, struct S8) = u8;
int main(void) {
    struct S1 a = {'A'}; struct S3 b = {1, 2, 3}; struct S6 c = {10, 20, 30}; struct S8 d = {2, 3}, e = {4, 5};
    struct D f = {1.5}; struct S12 g = {1, 2, 3};
    printf("%d %d %d %d %d\n", u1(a), u3(b, 4), u6(1, c), u8(d, e), fp(e, d));
    printf("%g %d %d\n", ud(f, 2.25), u12(g, b), many(1, 2, 3, 4, 5, 6, 7, 8, 9, d, b, c));
    printf("%d %d\n", va(1, 3, 4), u8(mk8(1, 2), mk8(3, 4)));
    return u3(mk3(), 0) % 100;
}
