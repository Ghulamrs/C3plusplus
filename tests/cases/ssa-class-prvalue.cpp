// A class prvalue materialised where an object is wanted ([class.temporary], M3b.2's SSA lowering): a cast to a class
// through its converting constructor - `(M)v`, `static_cast<M>(v)` - as a local's initialiser, an argument by value, a
// value bound to a const reference and a return; `T(o)` of a class with constructors copied from an object of it or of
// a class derived from it; and `->` on a class value through its operator->, hop by hop, on an lvalue, a call's result,
// and a hop that answers with another class. The ledger counts the one class with a destructor.
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct V { int x; V(int a) : x(a) {} };
struct M { int y; M(const V &v) : y(v.x * 10) {} };
struct W { int z; template <class T> W(const T &t) : z((int)t + 1) {} };

struct G {
    int g;
    G(int a) : g(a) { lfBuilt(this, "G"); }
    G(const G &o) : g(o.g + 100) { lfBuilt(this, "G"); }
    ~G() { lfGone(this, "G"); }
};

static int takeM(M m) { return m.y; }
static int refM(const M &m) { return m.y + 1; }
static M makeM(const V &v) { return (M)v; }
static int refG(const G &g) { return g.g; }
static G makeG(int a) { return (G)a; }

static void casts() {
    V v(4);
    M a = (M)v;
    M b = static_cast<M>(v);
    W c = (W)2.5;
    W d = static_cast<W>(7);
    printf("casts %d %d %d %d", a.y, b.y, c.z, d.z);
    printf(" %d", takeM((M)v));
    printf(" %d", refM(static_cast<M>(v)));
    printf(" %d", makeM(v).y);
    printf(" %d", refG((G)4));
    G e = makeG(5);
    printf(" %d", e.g % 100);
    { int inner = refG(static_cast<G>(6)) + ((G)7).g; printf(" %d", inner); }
    printf("\n");
}

struct B { int x; B(int a) : x(a) {} };
struct D : B { int y; D(int a) : B(a), y(7) {} };
static int takeB(B b) { return b.x; }
static int refB(const B &b) { return b.x * 2; }

static void copies() {
    B b(3);
    D d(5);
    printf("copies %d %d", takeB(B(b)), takeB(B(d)));
    printf(" %d %d", refB(B(b)), refB(B(d)));
    printf(" %d\n", B(d).x + B(b).x);
}

struct Node { int v; int get() const { return v; } };
struct Ptr {
    Node *p;
    Node *operator->() const { return p; }
};
struct Holder {
    Ptr ps[2];
    const Ptr &operator[](int i) const { return ps[i]; }
    Ptr make(int i) const { return ps[i]; }
};
struct Twice { Ptr q; Ptr operator->() const { return q; } };          // a hop answering with a class, asked again
struct TwiceRef { Ptr q; const Ptr &operator->() const { return q; } }; // and one answering with a reference to one

static int readByValue(const Holder &h) { return h.make(1)->v; }
static int readByOperator(const Holder &h) { return h[0]->v; }
static int callByValue(const Holder &h) { return h.make(0)->get(); }
static int callByOperator(const Holder &h) { return h[1]->get(); }
static bool inCondition(const Holder &h, int k) { return h[0]->v == k && h.make(1)->get() != k; }
static int throughLocal(Ptr p) { p->v = p->v + 1; return p->v; }
static int twoHops(const Twice &t, const TwiceRef &r) { return t->v * 10 + r->get(); }

static void arrows() {
    Node a = {3}, b = {4};
    Holder h;
    h.ps[0].p = &a;
    h.ps[1].p = &b;
    const int r = readByValue(h) * 10 + readByOperator(h);
    const int c = callByValue(h) * 10 + callByOperator(h);
    printf("arrows %d %d %d %d", r, c, inCondition(h, 3) ? 1 : 0, inCondition(h, 4) ? 1 : 0);
    Twice t;
    t.q = h.ps[0];
    TwiceRef tr;
    tr.q = h.ps[1];
    printf(" %d", twoHops(t, tr));
    const int stepped = throughLocal(h.ps[1]);   // its own statement: it writes the b the next line reads
    printf(" %d %d\n", stepped, b.v);
}

int main() {
    lfWatch();
    casts();
    copies();
    arrows();
    return 0;
}
