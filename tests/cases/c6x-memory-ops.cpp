// M6a.2 W5: a class copy and a zero fill on the C6000 selector - words or doublewords where the class is aligned for
// them, halfwords and bytes where it is not, a base moved on past an offset's reach, and the guard bytes around each
// copy untouched; a class argument as its words, or by the address of the caller's copy, which the callee may change.
#include <stdio.h>

struct Word { int a; };
struct Three { int a, b, c; };
struct Dbl { double d; int k; };
struct Chars { char c[7]; };
struct Shorts { short s[5]; };
struct Big { int w[60]; };
struct BigD { double d[40]; };
struct Bytes { char c[45]; };
struct Guarded { int lo; Chars mid; char pad; int hi; };

void copyWord(Word &to, const Word &from) { to = from; }
void copyThree(Three *to, const Three *from) { *to = *from; }
void copyDbl(Dbl &to, const Dbl &from) { to = from; }
void copyChars(Chars &to, const Chars &from) { to = from; }
void copyShorts(Shorts &to, const Shorts &from) { to = from; }
void copyBig(Big &to, const Big &from) { to = from; }
void copyBigD(BigD &to, const BigD &from) { to = from; }
void copyBytes(Bytes &to, const Bytes &from) { to = from; }
void copyMid(Guarded &to, const Guarded &from) { to.mid = from.mid; }

void zeroThree(Three &t) { t = Three(); }
void zeroDbl(Dbl &t) { t = Dbl(); }
void zeroChars(Chars &t) { t = Chars(); }
void zeroShorts(Shorts &t) { t = Shorts(); }
void zeroBytes(Bytes &t) { t = Bytes(); }
void zeroMid(Guarded &g) { g.mid = Chars(); }
int zeroLocal(int n) {
    Three t = Three();
    Dbl d = Dbl();
    int a[9] = { n };
    return t.a + t.b + t.c + (int)d.d + d.k + a[0] + a[8];
}
int copyLocal(int n) {
    Three t = { n, n + 1, n + 2 };
    Three u = t;
    Three v;
    v = u;
    return v.a * 100 + v.b * 10 + v.c;
}

struct Two { int a, b; };
int byWord(Word w, int k) { w.a += k; return w.a; }
int byTwo(int k, Two t) { t.a *= k; return t.a + t.b; }
int byThree(Three t, int k, Three u) { t.a += k; u.c -= k; return t.a * 1000 + t.b * 100 + u.b * 10 + u.c; }
double byDbl(Dbl d, Two t) { d.k += t.b; return d.d * d.k; }
struct Empty {};
struct Ch3 { char c[3]; };
struct Sh3 { short s[3]; };
int byEmpty(Empty, int k, Empty) { return k + 1; }
int byCh3(Ch3 c, Sh3 h, int k) { c.c[1] = 9; return c.c[0] * 100 + c.c[1] * 10 + c.c[2] + h.s[0] + h.s[2] * k; }
int byBig(const Big &r, Big b) { b.w[0] = 99; return b.w[0] + b.w[59] + r.w[0]; }

unsigned sum(const char *p, int n) { unsigned s = 0; for (int k = 0; k < n; k++) s = s * 31 + p[k]; return s; }

int main() {
    Word w1 = { 7 }, w2 = { 0 };
    copyWord(w2, w1);
    Three t1 = { 1, 2, 3 }, t2 = { 0, 0, 0 };
    copyThree(&t2, &t1);
    Dbl d1 = { 2.5, 9 }, d2 = { 0, 0 };
    copyDbl(d2, d1);
    Chars c1 = { { 'a', 'b', 'c', 'd', 'e', 'f', 'g' } }, c2;
    copyChars(c2, c1);
    Shorts s1 = { { 1, -2, 3, -4, 5 } }, s2;
    copyShorts(s2, s1);
    Big b1, b2;
    for (int k = 0; k < 60; k++) { b1.w[k] = k * k; b2.w[k] = 0; }
    copyBig(b2, b1);
    BigD e1, e2;
    for (int k = 0; k < 40; k++) { e1.d[k] = k * 0.5; e2.d[k] = 0; }
    copyBigD(e2, e1);
    Bytes y1, y2;
    for (int k = 0; k < 45; k++) { y1.c[k] = (char)(k + 33); y2.c[k] = 0; }
    copyBytes(y2, y1);
    Guarded g1 = { 11, { { 'p', 'q', 'r', 's', 't', 'u', 'v' } }, 'x', 22 }, g2 = { 33, { { 0 } }, 'y', 44 };
    copyMid(g2, g1);
    printf("%d %d %d %d %g %d\n", w2.a, t2.a, t2.b, t2.c, d2.d, d2.k);
    printf("%u %d %d\n", sum(c2.c, 7), s2.s[0] + s2.s[1] * 10 + s2.s[4] * 100, b2.w[59] + b2.w[31]);
    printf("%g %g %u\n", e2.d[39], e2.d[17], sum(y2.c, 45));
    printf("%d %u %c %d\n", g2.lo, sum(g2.mid.c, 7), g2.pad, g2.hi);
    zeroThree(t1);
    zeroDbl(d1);
    zeroChars(c1);
    zeroShorts(s1);
    zeroBytes(y1);
    zeroMid(g1);
    printf("%d %d %d %g %d\n", t1.a, t1.b, t1.c, d1.d, d1.k);
    printf("%u %d %u\n", sum(c1.c, 7), s1.s[0] + s1.s[4], sum(y1.c, 45));
    printf("%d %u %c %d\n", g1.lo, sum(g1.mid.c, 7), g1.pad, g1.hi);
    printf("%d %d\n", zeroLocal(5), copyLocal(4));
    Two p = { 6, 7 };
    Three q = { 1, 2, 3 }, r = { 4, 5, 6 };
    Dbl e = { 1.5, 2 };
    printf("%d %d %d %g %d\n", byWord(w1, 3), byTwo(2, p), byThree(q, 5, r), byDbl(e, p), byBig(b1, b1));
    printf("%d %d %d %d %d %d\n", w1.a, p.a, q.a, r.c, e.k, b1.w[0]);
    Empty m;
    Ch3 c3 = { { 1, 2, 3 } };
    Sh3 h3 = { { 1000, -1, 7 } };
    printf("%d %d %d %d\n", byEmpty(m, 4, m), byCh3(c3, h3, 3), c3.c[1], h3.s[1]);
    return 0;
}
