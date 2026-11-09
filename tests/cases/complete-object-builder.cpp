// The complete object's compiler-written functions the SSA Builder lowers (M3b.3, lane B): the deleting destructor a
// `delete` through a base pointer reaches - Itanium's D0, cl's ??_G - freeing with the platform's operator delete or the
// class's own; and the C1 and D1 of a class with virtual bases, cl's ??_D: built by default, copied out of another
// object, a virtual base with a virtual base of its own, and a later part throwing after a virtual base was built.
// Every count is the ledger's; no copy here is elidable and nothing depends on the order arguments are evaluated in.
#include "lifetime.h"
#include <cstdlib>
extern "C" int printf(const char *, ...);
struct M { int v; M(); explicit M(int x); M(const M &o); ~M(); };
M::M() : v(1) { lfBuilt(this, "M"); printf("+M "); }
M::M(int x) : v(x) { if (x > 100) { printf("!M%d ", x); throw x; } lfBuilt(this, "M"); printf("+M%d ", v); }
M::M(const M &o) : v(o.v) { lfBuilt(this, "M"); printf("+M%dc ", v); }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
// A polymorphic base and two derived classes, deleted through the base: the deleting destructor of each is reached by
// the slot, and Owned's frees with its own operator delete.
static int freed = 0;
struct Base { int b; Base(); virtual ~Base(); };
Base::Base() : b(2) { lfBuilt(this, "Base"); printf("+Base "); }
Base::~Base() { printf("-Base "); lfGone(this, "Base"); }
struct Derived : Base { M m; Derived(); ~Derived(); };
Derived::Derived() { lfBuilt(this, "Derived"); printf("+Derived "); }
Derived::~Derived() { printf("-Derived "); lfGone(this, "Derived"); }
struct Owned : Base {
    M m;
    Owned() : m(4) { lfBuilt(this, "Owned"); printf("+Owned "); }
    ~Owned() { printf("-Owned "); lfGone(this, "Owned"); }
    static void *operator new(size_t n) { return malloc(n); }
    static void operator delete(void *p) { freed++; printf("own-delete "); free(p); }
};
struct Quiet : Base { int q; };   // the destructor the compiler writes, and the deleting one beside it
// A virtual base shared by two paths: C1 builds the one V before A's and B's parts, D1 destroys it after them.
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
struct Imp : A { int k; };    // the implicit constructor and destructor of a class with a virtual base, split too
// A member that throws after the virtual base was built: C1 destroys the V it built and nothing else.
struct Late : virtual V { M lm; Late(int x); ~Late(); };
Late::Late(int x) : lm(x) { lfBuilt(this, "Late"); printf("+Late "); }
Late::~Late() { printf("-Late "); lfGone(this, "Late"); }
// A polymorphic class with a virtual base: its deleting destructor reaches D1 (cl's ??_D) and then frees.
struct Poly : virtual V { M pm; Poly(); virtual ~Poly(); };
Poly::Poly() : pm(8) { lfBuilt(this, "Poly"); printf("+Poly "); }
Poly::~Poly() { printf("-Poly "); lfGone(this, "Poly"); }
int main() {
    lfWatch();
    {
        Base *p = new Derived;
        printf("| %d ", p->b);
        delete p;
        p = new Owned;
        delete p;
        p = new Quiet;
        delete p;
        printf("| freed %d\n", freed);
    }
    {
        Dia d;
        printf("| %d %d %d %d\n", d.v, d.a, d.bm.v, d.m.v);
        Imp i;
        printf("| %d %d\n", i.v, i.a);
    }
    {
        int caught = 0;
        try { Late l(200); printf("unreached"); } catch (int x) { caught = x; }
        printf("| caught %d live %d\n", caught, lfLive());
        Late ok(11);
        printf("| %d %d\n", ok.v, ok.lm.v);
    }
    {
        Poly *p = new Poly;
        printf("| %d %d ", p->v, p->pm.v);
        delete p;
        printf("|\n");
    }
    return 0;
}
