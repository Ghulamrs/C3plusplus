// using B::B with references, rvalue refs, pointers, arrays, defaults; the ledger counts objects.
#include "../../../tests/cases/lifetime.h"
extern "C" int printf(const char *, ...);
struct M { int v; M() : v(0) { lfBuilt(this, "M"); } M(int x) : v(x) { lfBuilt(this, "M"); } M(const M &o) : v(o.v) { lfBuilt(this, "M"); } ~M() { lfGone(this, "M"); } };
struct B {
    int a; double d; const char *s; int *p; int cnt;
    B(int x) : a(x), d(0), s(0), p(0), cnt(1) { lfBuilt(this, "B"); }
    B(int x, double y, const char *z) : a(x), d(y), s(z), p(0), cnt(2) { lfBuilt(this, "B"); }
    B(long &r) : a((int)r), d(0), s(0), p(0), cnt(3) { r++; lfBuilt(this, "B"); }
    B(M &&m) : a(m.v), d(0), s(0), p(0), cnt(4) { m.v = -1; lfBuilt(this, "B"); }
    B(const M &m, int k = 5) : a(m.v + k), d(0), s(0), p(0), cnt(5) { lfBuilt(this, "B"); }
    B(int (&arr)[3]) : a(arr[0] + arr[1] + arr[2]), d(0), s(0), p(arr), cnt(6) { lfBuilt(this, "B"); }
    B(int B::*mp, int k) : a(k), d(0), s(0), p(0), cnt(7) { a = this->*mp + k; lfBuilt(this, "B"); }
    B(const B &o) : a(o.a), d(o.d), s(o.s), p(o.p), cnt(8) { lfBuilt(this, "B"); }
    ~B() { lfGone(this, "B"); }
};
struct D : B {
    using B::B;
    M m;
    int own;
    ~D() { lfGone(this, "D"); }
};
int main() {
    lfWatch();
    { D d(7); printf("%d %d %d\n", d.a, d.cnt, d.m.v); }
    { D d(1, 2.5, "s"); printf("%d %g %s %d\n", d.a, d.d, d.s, d.cnt); }
    { long r = 10; D d(r); printf("%d %ld %d\n", d.a, r, d.cnt); }
    { M m(9); D d(static_cast<M &&>(m)); printf("%d %d %d\n", d.a, m.v, d.cnt); }
    { M m(9); D d(m); printf("%d %d %d\n", d.a, m.v, d.cnt); D e(m, 1); printf("%d %d\n", e.a, e.cnt); }
    { int arr[3] = {1, 2, 3}; D d(arr); printf("%d %d %d\n", d.a, d.p == arr, d.cnt); }
    { D d(&B::cnt, 4); printf("%d %d\n", d.a, d.cnt); }
    { D d(7); D e(d); printf("%d %d %d\n", e.a, e.cnt, e.m.v); }
    { D *d = new D(3); printf("%d %d\n", d->a, d->cnt); delete d; }
    return 0;
}
