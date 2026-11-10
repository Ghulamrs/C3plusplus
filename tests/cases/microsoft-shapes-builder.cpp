// The shapes lane E of M3b.3 lowers through the SSA Builder that the Microsoft ABI has of its own, run on every target:
// a namespace-scope object and a static local with destructors (cl's atexit helper ??__F), a jump out of nested handlers
// (each a funclet there), a class built as a handler's result, a virtual function reached through a pointer to member (cl's
// vcall thunk ??_9), a virtual base's member and the conversion to it (cl's vbtable), and a constructor's by-value parameter
// with a destructor (which cl's callee destroys). Every count is the ledger's; no count here depends on elision.
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct P {
    int v;
    P(int x) : v(x) { lfBuilt(this, "P"); }
    P(const P &o) : v(o.v) { lfBuilt(this, "P"); }
    ~P() { lfGone(this, "P"); }
};

// The report registered before the objects below, so it runs after their destructors (dynamic-init.cpp's watcher).
struct Watcher { Watcher(); };
Watcher::Watcher() { lfWatch(); }
static Watcher watcher;
P global(7);

static int staticLocal(int k) {
    static P kept(40);
    return kept.v + k;
}

// A break out of a loop from a handler inside a handler, and a return out of both.
static int nestedBreak() {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        try { throw i; } catch (int a) {
            P held(a);
            try { throw a + 10; } catch (int b) {
                P inner(b);
                n += inner.v;
                if (a == 1) break;
            }
        }
    }
    return n;
}

static int nestedReturn(int k) {
    try { throw k; } catch (int a) {
        P held(a);
        try { throw a * 2; } catch (int b) {
            P inner(b);
            if (b > 4) return inner.v + held.v;
        }
    }
    return -1;
}

// Not on the ledger: whether the result is copied is the implementation's choice, and only its value is read.
struct Q {
    int v;
    Q(int x) : v(x) {}
    Q(const Q &o) : v(o.v) {}
    ~Q() { v = -1; }
};
static Q builtInHandler(int k) {
    try { throw k; } catch (int a) {
        return Q(a + 100);
    }
    return Q(0);
}

struct V {
    int base;
    V(int b) : base(b) {}
    virtual ~V() {}
    virtual int f() { return base + 1; }
    virtual int g(int x) { return base * x; }
};
struct W : V {
    W() : V(5) {}
    int f() { return base + 50; }
};

struct VB { int x; VB() : x(3) {} };
struct D1 : virtual VB { int d; D1() : d(4) {} };
struct D2 : virtual VB { int e; D2() : e(6) {} };
struct Dia : D1, D2 { int z; Dia() : z(9) {} };

static int throughVbase(D1 *p) { return p->x + p->d; }
static int toVbase(D2 *p) { VB *b = p; return b->x; }

struct H {
    int sum;
    H(P a, int k) : sum(a.v + k) { lfBuilt(this, "H"); }
    ~H() { lfGone(this, "H"); }
};

int main() {
    printf("global %d\n", global.v);
    int s = staticLocal(1);
    s += staticLocal(2);
    printf("static %d\n", s);
    printf("break %d\n", nestedBreak());
    printf("return %d\n", nestedReturn(3));
    {
        Q r = builtInHandler(5);
        printf("handler %d\n", r.v);
    }
    W w;
    int (V::*pf)() = &V::f;
    int (V::*pg)(int) = &V::g;
    V *pv = &w;
    int viaF = (pv->*pf)();
    int viaG = (pv->*pg)(3);
    printf("vcall %d %d\n", viaF, viaG);
    Dia dia;
    dia.x = 11;
    int a = throughVbase(&dia);
    int b = toVbase(&dia);
    printf("vbase %d %d %d\n", a, b, dia.z);
    P arg(20);
    {
        H h(arg, 2);
        printf("byvalue %d\n", h.sum);
    }
    return 0;
}
