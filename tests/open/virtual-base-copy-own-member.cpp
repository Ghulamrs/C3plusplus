// The copy constructor the compiler writes for a class with a virtual base copies the bases out of the source and then
// skips the class's own member: `Dia e(d)` leaves `e.m` unbuilt (its value the stack's) and `~Dia` destroys it - the ledger's
// phantom=1 where clang's is 0. Both ABIs, at every level, on the Ast.h path and under -front=ssa, which calls the same
// implicit copy (the "copy or move" kind, M3b.3). Found by lane B's complete-object-builder case, 2026-11-09; older than it.
#include "../cases/lifetime.h"
extern "C" int printf(const char *, ...);
struct M { int v; M(); explicit M(int x); M(const M &o); ~M(); };
M::M() : v(1) { lfBuilt(this, "M"); printf("+M "); }
M::M(int x) : v(x) { lfBuilt(this, "M"); printf("+M%d ", v); }
M::M(const M &o) : v(o.v) { lfBuilt(this, "M"); printf("+M%dc ", v); }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
struct V { int v; V(); V(const V &o); ~V(); };
V::V() : v(7) { lfBuilt(this, "V"); printf("+V "); }
V::V(const V &o) : v(o.v) { lfBuilt(this, "V"); printf("+Vc "); }
V::~V() { printf("-V "); lfGone(this, "V"); }
struct A : virtual V { int a; A(); A(const A &o); ~A(); };
A::A() : a(v + 1) { lfBuilt(this, "A"); printf("+A "); }
A::A(const A &o) : a(o.a) { lfBuilt(this, "A"); printf("+Ac "); }
A::~A() { printf("-A "); lfGone(this, "A"); }
struct B : virtual V { M bm; B(); B(const B &o); ~B(); };
B::B() : bm(3) { lfBuilt(this, "B"); printf("+B "); }
B::B(const B &o) : bm(o.bm) { lfBuilt(this, "B"); printf("+Bc "); }
B::~B() { printf("-B "); lfGone(this, "B"); }
struct Dia : A, B { M m; Dia(); ~Dia(); };   // copied by the constructor the compiler writes: its parts are in the ledger, it is not
Dia::Dia() : m(5) { printf("+Dia "); }
Dia::~Dia() { printf("-Dia "); }
int main() {
    lfWatch();
    Dia d;
    d.v = 70;
    Dia e(d);
    printf("| %d %d %d %d\n", e.v, e.a, e.bm.v, e.m.v);
    return 0;
}
