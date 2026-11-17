// What the SSA lowering refused and the C6000's walker wrote until R1 (M6a.2, 2026-11-18): each family once, so the selector has it.
#include <cstdio>
#include <initializer_list>
struct A { int v; A(int x = 7); };
A::A(int x) : v(x) {}
struct D : A { D(); };
D::D() : A(3) {}
struct C { int v; C(int x = 7); C(const C &o); ~C(); };
C::C(int x) : v(x) {}
C::C(const C &o) : v(o.v) {}
C::~C() {}
struct K { int v; K(int x); ~K(); };
K::K(int x) : v(x) {}
K::~K() {}
struct L { ~L(); };
L::~L() { printf("~L\n"); }
struct P { int v; };
struct Q { int k; operator P() const; };
Q::operator P() const { P p = { k * 2 }; return p; }
struct S { static int m; int v; };
int S::m = 4;
struct M { int v; M(int x); M(const M &o); };
M::M(int x) : v(x) {}
M::M(const M &o) : v(o.v + 1) {}
struct B { M m; unsigned b : 3; B(); };
B::B() : m(1), b(5) {}
struct F { int b : 3; int &r; };
struct R { const A &r; R(const D &d); };
R::R(const D &d) : r(d) {}
int g = 2;

decltype(nullptr) none() { return nullptr; }
C defaulted() { C a; a.v++; return a; }
C braced() { C a{3}; a.v++; return a; }
C handled(int k) { try { if (k) throw 1; } catch (int) { C a(3); a.v++; return a; } return C(1); }
K caught() { try { throw 1; } catch (int) { return K(4); } }
P beside(P *p) { L l; return *p; }
A sliced(int k) { D d; A a(k); if (k) return d; return a; }
S make() { S s; s.v = 1; return s; }
int allowed(int k) throw(int) { if (k) throw k; return 0; }
int specified(int k) throw(int, char) { try { return allowed(k); } catch (char) { return -1; } }

int main() {
    int *p = none();
    printf("%d\n", p == 0);
    printf("%d %d\n", defaulted().v, braced().v);
    printf("%d %d\n", handled(0).v, handled(1).v);
    printf("%d\n", caught().v);
    P a = { 3 };
    printf("%d\n", beside(&a).v);
    printf("%d %d\n", sliced(1).v, sliced(0).v);
    A t = D();
    Q q = { 4 };
    P c = q;
    printf("%d %d\n", t.v, c.v);
    S s;
    printf("%d %d\n", make().m, (s).m);
    B b1;
    B b2(b1);
    printf("%d %u\n", b2.m.v, b2.b);
    F f1 = { 1, g };
    F f2(f1);
    printf("%d %d\n", f2.b, f2.r);
    D d1, d2;
    R r(d1);
    std::initializer_list<A> l = { d1, d2 };
    printf("%d %d\n", r.r.v, (int)l.size() + l.begin()->v);
    try { specified(5); } catch (int e) { printf("threw %d\n", e); }
    printf("%d\n", specified(0));
    return 0;
}
