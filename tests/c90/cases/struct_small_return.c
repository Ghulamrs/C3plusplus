// expect: 6
/* A struct of 8 bytes or less comes back in registers on most targets (A5:A4
   on the C6000, rax/rdx, x0/x1), a larger one through the hidden pointer;
   every size from 1 to 8, the odd ones at odd addresses, a double, a float,
   a pointer, one of 12 bytes, and results nobody reads. */
#include <stdio.h>
struct S1 { char a; }; struct S2 { short a; }; struct S3 { char a, b, c; };
struct S4 { int a; }; struct S5 { char a[5]; }; struct S6 { short a, b, c; };
struct S7 { char a[7]; }; struct S8 { int a, b; }; struct D { double d; };
struct F { float f; }; struct S12 { int a, b, c; }; struct P { char *p; };
static struct S1 m1(void) { struct S1 s = {'x'}; return s; }
static struct S2 m2(void) { struct S2 s = {-1234}; return s; }
static struct S3 m3(void) { struct S3 s = {1, 2, 3}; return s; }
static struct S4 m4(int v) { struct S4 s; s.a = v; return s; }
static struct S5 m5(void) { struct S5 s = {"abcd"}; return s; }
static struct S6 m6(void) { struct S6 s = {10, 20, 30}; return s; }
static struct S7 m7(void) { struct S7 s = {"123456"}; return s; }
static struct S8 m8(int a, int b) { struct S8 s; s.a = a; s.b = b; return s; }
static struct D md(double d) { struct D s; s.d = d * 2; return s; }
static struct F mf(float f) { struct F s; s.f = f + 1; return s; }
static struct S12 m12(int a) { struct S12 s; s.a = a; s.b = a + 1; s.c = a + 2; return s; }
static struct P mp(char *p) { struct P s; s.p = p + 1; return s; }
static struct S8 twice(struct S8 s) { return m8(s.b, s.a); }
static struct S8 (*fp)(int, int) = m8;
int main(void) {
    char pad; struct S3 t3; struct S8 t8; struct S12 t12; int guard[2] = {77, 88};
    char buf[4] = "abc";
    pad = 'p';
    printf("%c %d\n", m1().a, m2().a);
    t3 = m3(); printf("%d %d %d %c\n", t3.a, t3.b, t3.c, pad);
    printf("%d %s\n", m4(42).a, m5().a);
    printf("%d %d %d %s\n", m6().a, m6().b, m6().c, m7().a);
    t8 = twice(m8(3, 4)); printf("%d %d %d\n", t8.a, t8.b, fp(5, 6).b);
    printf("%g %g %c\n", md(1.25).d, mf(2.5f).f, *mp(buf).p);
    t12 = m12(5); printf("%d %d %d\n", t12.a, t12.b, t12.c);
    m12(9); m8(1, 2); m3();
    printf("%d %d\n", guard[0], guard[1]);
    return m8(0, 3).b + m3().c;
}
