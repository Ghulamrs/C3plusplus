// The two compiler-written special members the SSA Builder lowers first (M3b.3): the implicit default constructor -
// bases in order, the vptr, each member by its own initialiser or its default constructor, an array member element by
// element - and the implicit destructor, [class.dtor]/8's reverse of all of it. Every count is the ledger's, no copy is
// made anywhere, and nothing here depends on the order arguments are evaluated in.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
int serial = 0;
struct M { int v; M(); explicit M(int x); ~M(); };
M::M() : v(++serial) { lfBuilt(this, "M"); printf("+M%d ", v); }
M::M(int x) : v(x) { lfBuilt(this, "M"); printf("+M%d ", v); }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
struct B { int tag; B(); ~B(); };
B::B() : tag(5) { lfBuilt(this, "B"); printf("+B "); }
B::~B() { printf("-B "); lfGone(this, "B"); }
struct C { C(); ~C(); };
C::C() { lfBuilt(this, "C"); printf("+C "); }
C::~C() { printf("-C "); lfGone(this, "C"); }
struct Plain { int a; double d; };
// Two bases, a member built by default, an initialised scalar, an array of two, an aggregate from its list, a zeroed
// scalar and a member built from its own braces: the compiler writes both special members of this class.
struct Both : B, C { M first; int k = 40; M pair[2]; Plain p = { 1, 2.5 }; int zero{}; M last{ 9 }; };
// The implicit constructor of a polymorphic class stores its vptr after its bases and before its members, and the
// derived class's stores its own over it ([class.cdtor]/4): a virtual call from each level answers for that level.
struct Poly { virtual int id() const; M m; };
int Poly::id() const { return 1; }
struct Derived : Poly { int id() const; M dm{ 3 }; };
int Derived::id() const { return 2; }
struct Holder { Derived d; Poly alone; };
int main() {
    lfWatch();
    {
        Both b;
        printf("| %d %d %g %d %d\n", b.tag, b.k, b.p.d, b.zero, b.last.v);
    }
    printf("\n");
    {
        Holder h;
        const Poly &p = h.d;
        printf("| %d %d\n", p.id(), h.alone.id());
    }
    printf("\n");
    return 0;
}
