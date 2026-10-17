// Objects under the ledger: every inherited construction destroys once. Elision-free by construction.
#include "../../../tests/cases/lifetime.h"
extern "C" int printf(const char *, ...);
struct M { int v; M() : v(1) { lfBuilt(this, "M"); } M(const M &o) : v(o.v) { lfBuilt(this, "M"); } ~M() { lfGone(this, "M"); } };
struct B { int a; B(int x) : a(x) { lfBuilt(this, "B"); } B(const M &m) : a(m.v) { lfBuilt(this, "B"); } B(const B &o) : a(o.a) { lfBuilt(this, "B"); } ~B() { lfGone(this, "B"); } };
struct D : B { using B::B; M m; D(const D &o) : B(o), m(o.m) { lfBuilt(this, "D"); } ~D() { lfGone(this, "D"); } };
int sum(const D &d) { return d.a + d.m.v; }
int main() {
    lfWatch();
    { D d(7); printf("%d\n", sum(d)); }
    { M m; D d(m); printf("%d\n", sum(d)); }
    { D *p = new D(2); printf("%d\n", sum(*p)); delete p; }
    return 0;
}
