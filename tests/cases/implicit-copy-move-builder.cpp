// The compiler-written functions lane A of M3b.3 lowers through the SSA Builder: the implicit copy and move constructors
// and assignments ([class.copy]/15, /28) - a base copied by its own copy, a trivially copyable base copied as bytes, a
// polymorphic class whose copy stores the vptr and whose assignment does not, an array member copied element by element,
// a member moved where it has a move constructor and copied where it has only a copy - and the array loops: `new T[n]`
// built and `delete[]`d, a static array destroyed at exit by the helper, and a constructor throwing mid-array, the elements
// built so far destroyed last first. Every copy here is written out, so elision has nothing to take; every count is the ledger's.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
struct M {
    int v;
    M();
    M(int x);
    M(const M &o);
    M &operator=(const M &o);
    ~M();
};
M::M() : v(0) { lfBuilt(this, "M"); printf("+M0 "); }
M::M(int x) : v(x) { lfBuilt(this, "M"); printf("+M%d ", v); }
M::M(const M &o) : v(o.v + 100) { lfBuilt(this, "M"); printf("cM%d ", v); }
M &M::operator=(const M &o) { v = o.v + 200; printf("=M%d ", v); return *this; }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
// A class with a move constructor: an implicit move of a class holding one moves it, and copies an M beside it.
struct Mv {
    int v;
    Mv(int x) : v(x) { lfBuilt(this, "Mv"); }
    Mv(const Mv &o) : v(o.v + 1000) { lfBuilt(this, "Mv"); printf("cMv%d ", v); }
    Mv(Mv &&o) : v(o.v + 2000) { o.v = -1; lfBuilt(this, "Mv"); printf("mMv%d ", v); }
    ~Mv() { lfGone(this, "Mv"); }
};
struct Base { M bm; int tag; Base(int t) : bm(t), tag(t) {} };     // its own implicit copy: the derived copy calls it
struct Trivial { int a, b; };                                      // copied as bytes, members and all
struct Both : Trivial, Base {
    M one;
    int k;
    M pair[2];
    short digits[3];
    Both(int s) : Base(s), one(s + 1), k(s + 2) { pair[0].v = s + 3; pair[1].v = s + 4; a = s + 5; b = s + 6; digits[0] = 7; digits[1] = 8; digits[2] = 9; }
};
struct Poly { virtual int id() const; M m; Poly(int x) : m(x) {} };
int Poly::id() const { return 1; }
struct Derived : Poly { int id() const; Derived(int x) : Poly(x) {} };
int Derived::id() const { return 2; }
struct Pair { Mv a; M b; Pair(int x) : a(x), b(x + 1) {} };
// An element constructor that throws on the third element: the loop destroys the two built, last first.
struct Boom {
    static int made;
    int v;
    Boom() : v(++made) { if (v == 3) throw 1; lfBuilt(this, "Boom"); printf("+Boom%d ", v); }
    ~Boom() { printf("-Boom%d ", v); lfGone(this, "Boom"); }
};
int Boom::made = 0;
// Outside the ledger, which reports at exit before this array's destruction: its elements print on their own.
struct Q { int v; Q(); ~Q(); };
static int qs = 0;
Q::Q() : v(++qs) { printf("+Q%d ", v); }
Q::~Q() { printf("-Q%d ", v); }
Q quiet[3];
int main() {
    lfWatch();
    {
        Both x(10);
        printf("\n");
        Both y(x);
        printf("| %d %d %d %d %d %d %d %d %d %d\n", y.a, y.b, y.tag, y.bm.v, y.one.v, y.k, y.pair[0].v, y.pair[1].v, y.digits[0] + y.digits[1], y.digits[2]);
        Both z(30);
        printf("\n");
        z = x;
        printf("| %d %d %d %d %d %d %d %d\n", z.a, z.tag, z.bm.v, z.one.v, z.k, z.pair[0].v, z.pair[1].v, z.digits[1]);
    }
    printf("\n");
    {
        Derived d(5);
        Derived e(d);
        const Poly &p = e;
        Poly q(7);
        q = d;
        printf("| %d %d %d %d\n", p.id(), e.m.v, q.id(), q.m.v);
    }
    printf("\n");
    {
        Pair p(1);
        Pair c(p);
        Pair m(static_cast<Pair &&>(p));
        printf("| %d %d %d %d %d\n", c.a.v, c.b.v, m.a.v, m.b.v, p.a.v);
    }
    printf("\n");
    {
        M *arr = new M[3];
        printf("| %d %d %d\n", arr[0].v, arr[1].v, arr[2].v);
        delete[] arr;
    }
    printf("\n");
    try {
        Boom *b = new Boom[5];
        printf("| %d\n", b->v);
    } catch (int e) {
        printf("| caught %d\n", e);
    }
    printf("| %d %d %d\n", quiet[0].v, quiet[1].v, quiet[2].v);
    return 0;
}
