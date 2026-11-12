// Temporaries that outlive the call they were built for, under the object
// ledger: a `new` whose constructor binds a temporary with a destructor
// (the argument dies at the full expression's end, after the constructor, and
// the storage is freed only if the constructor throws); a return built from a
// call given such a temporary; a reference bound to a call's lvalue result.
extern "C" int printf(const char *, ...);
#include "lifetime.h"

struct Tracked {
    int v;
    Tracked(int x);
    Tracked(const Tracked &o);
    ~Tracked();
};
Tracked::Tracked(int x) : v(x) { lfBuilt(this, "Tracked"); }
Tracked::Tracked(const Tracked &o) : v(o.v) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }

struct Holder {
    int v;
    Holder(const Tracked &t, int bad);
};
Holder::Holder(const Tracked &t, int bad) : v(t.v) { if (bad) throw 7; }

struct Pair {
    int a, b;
    Pair(int x, int y);
};
Pair::Pair(int x, int y) : a(x), b(y) {}

struct Owned {
    int v;
    Owned(int x);
    Owned(const Owned &o);
    ~Owned();
};
Owned::Owned(int x) : v(x) { lfBuilt(this, "Owned"); }
Owned::Owned(const Owned &o) : v(o.v) { lfBuilt(this, "Owned"); }
Owned::~Owned() { lfGone(this, "Owned"); }

static int table[4] = { 1, 2, 3, 4 };

Pair makePair(const Tracked &t) { return Pair(t.v, t.v + 1); }
Pair viaCall(int x) { return makePair(Tracked(x)); }
Owned viaOwned(int x) { return Owned(Tracked(x).v * 2); }
int &slotOf(const Tracked &t) { return table[t.v]; }

int main() {
    lfWatch();
    Holder *h = new Holder(Tracked(5), 0);
    printf("%d\n", h->v);
    delete h;
    try {
        Holder *g = new Holder(Tracked(6), 1);
        printf("%d\n", g->v);
    } catch (int e) {
        printf("caught %d\n", e);
    }
    Pair p = viaCall(8);
    printf("%d %d\n", p.a, p.b);
    Owned o = viaOwned(9);
    printf("%d\n", o.v);
    int &r = slotOf(Tracked(2));
    r += 10;
    printf("%d %d\n", r, table[2]);
    return 0;
}
