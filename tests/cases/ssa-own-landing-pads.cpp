// Under -front=ssa (the .flags), the bodies whose only unwinding objects are temporaries of a declaration's
// initialiser or of a mem-initialiser - which the parser's cleanup regions leave out, so it gives the function no
// landing pads - get pads of their own, and the exception that leaves such a body destroys what it built; so does a
// thrown temporary's argument when the temporary's constructor throws (M3b.2, round 21). The parser's own body leaks
// them: tests/open/walker-declaration-temporary-unwind.cpp.
#include "lifetime.h"

struct T {
    int v;
    T(int x);
    T(const T &o);
    ~T();
};
T::T(int x) : v(x) { lfBuilt(this, "T"); }
T::T(const T &o) : v(o.v) { lfBuilt(this, "T"); }
T::~T() { lfGone(this, "T"); }

static int boom(const T &t) { if (t.v > 50) throw t.v; return t.v; }
static int boomR(T &&t) { if (t.v > 50) throw t.v + 1; return t.v * 2; }

struct Holds {
    int v;
    Holds(const T &t);
};
Holds::Holds(const T &t) : v(boom(t) + 3) {}
struct Pair {
    Holds h;
    int n;
    Pair(int k);
};
Pair::Pair(int k) : h(k), n(4) {}

struct Slot { int value; };
struct Table {
    Slot slots[4];
    Slot *at(const T &t);
    int &get(int i);
};
Slot *Table::at(const T &t) { if (t.v > 50) throw 70; return &slots[t.v % 4]; }
int &Table::get(int i) { Slot *s = at(T(i)); return s->value; }

static int temporary(int k) { int a = boom(T(k)); return a + 1; }
static int converted(int k) { int a = boom(k); int b = boomR(k + 1); return a + b; }
static int local(int k) { Holds h(k); return h.v; }
static int member(int k) { Pair p(k); return p.h.v + p.n; }

struct E {
    int v;
    E(const T &t);
};
E::E(const T &t) : v(boom(t) + 5) {}
static int raised(int k) { if (k > 0) throw E(k); return 0; }

static int caught = 0;
static void attempt(int (*f)(int), int k) {
    try {
        int r = f(k);
        printf("%d ", r);
    } catch (int e) {
        caught++;
        printf("caught %d ", e);
    } catch (const E &e) {
        caught++;
        printf("E %d ", e.v);
    }
}

int main() {
    lfWatch();
    attempt(temporary, 5);
    attempt(temporary, 60);
    attempt(converted, 6);
    attempt(converted, 51);
    attempt(converted, 50);
    attempt(local, 7);
    attempt(local, 80);
    attempt(member, 8);
    attempt(member, 90);
    attempt(raised, 9);
    attempt(raised, 95);
    Table t = { { { 0 }, { 0 }, { 0 }, { 0 } } };
    t.get(1) = 11;
    try {
        t.get(55) = 1;
    } catch (int e) {
        caught++;
        printf("caught %d ", e);
    }
    printf("| %d %d\n", t.slots[1].value, caught);
    return 0;
}
