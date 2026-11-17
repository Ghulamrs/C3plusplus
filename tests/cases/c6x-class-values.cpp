// M6a.2 W3: class values on the C6000 selector - a class result in A4 or A5:A4, through A3's pointer (null when the
// caller drops it) or through the hidden first parameter, and a class parameter in a register, a pair or by the address
// of the caller's copy. The ledger counts N; the callers that read a member of the result have no copy to make.
#include "lifetime.h"

struct S3 { char a; char b; char c; };
struct S4 { short a; char b; };
struct S8 { int a; int b; };
struct S12 { int a; int b; int c; };
struct N {
    int v;
    N(int x);
    N(const N &o);
    ~N();
    N twice(int k) const;
    S8 pair(int k) const;
};
N::N(int x) : v(x) { lfBuilt(this, "N"); }
N::N(const N &o) : v(o.v + 1) { lfBuilt(this, "N"); }
N::~N() { lfGone(this, "N"); }
N N::twice(int k) const { return N(v * 2 + k); }
S8 N::pair(int k) const { S8 s; s.a = v; s.b = k; return s; }

S3 r3(int x) { S3 s; s.a = (char)x; s.b = (char)(x + 1); s.c = (char)(x + 2); return s; }
S4 r4(int x) { S4 s; s.a = (short)x; s.b = 1; return s; }
S8 r8(int x) { S8 s; s.a = x; s.b = x + 1; return s; }
S12 r12(int x) { S12 s; s.a = x; s.b = x + 1; s.c = x + 2; return s; }
N rn(int x) { return N(x); }
N rn6(int a, int b, int c, int d, int e, int g) { return N(a + b * 2 + c * 3 + d * 4 + e * 5 + g * 6); }
S12 pick(int w, int x) { if (w) return r12(x); S12 s; s.a = 7; s.b = 8; s.c = 9; return s; }

int p3(int k, S3 s) { s.a = (char)(s.a + 1); return s.a + s.b + s.c + k; }
int p4(int k, S4 s) { return s.a + s.b + k; }
int p8(int k, S8 s) { s.b = s.b * 2; return s.a + s.b + k; }
int p12(int k, S12 s) { s.c = s.c + 100; return s.a + s.b + s.c + k; }
int pn(int k, N n) { return n.v + k; }
int pmix(S8 a, int k, S12 b, S4 c, int m) { return a.a + a.b * 2 + k * 3 + b.a + b.b + b.c + c.a + c.b + m; }
S8 swap8(S8 s) { S8 t; t.a = s.b; t.b = s.a; return t; }
S12 bump(S12 s, int k) { s.a = s.a + k; s.b = s.b + k; s.c = s.c + k; return s; }

int members(int x) { return r3(x).c + r4(x).a + r8(x).b + r12(x).c + rn(x).v + rn6(x, 1, 2, 3, 4, 5).v; }
int chain(int x) { return rn(x).twice(3).v + rn(x).pair(4).b; }
void drop(int x) { r12(x); r8(x); r4(x); rn(x); pick(1, x); }

int main() {
    lfWatch();
    S3 a = r3(10); S4 b = r4(20); S8 c = r8(30); S12 d = r12(40); N n = rn(50);
    printf("%d %d %d | %d %d | %d %d | %d %d %d | %d\n", a.a, a.b, a.c, b.a, b.b, c.a, c.b, d.a, d.b, d.c, n.v);
    printf("%d %d %d %d %d\n", p3(1, a), p4(2, b), p8(3, c), p12(4, d), pn(5, n));
    printf("%d %d %d | %d %d %d\n", a.a, c.b, d.c, pmix(c, 2, d, b, 9), swap8(c).a, bump(d, 5).c);
    S12 e = pick(0, 3), g = pick(1, 3);
    printf("%d %d %d %d\n", e.a, e.c, g.a, g.c);
    printf("%d %d %d\n", members(6), chain(7), rn6(1, 2, 3, 4, 5, 6).v);
    drop(8);
    return 0;
}
