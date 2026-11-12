// An aggregate built member by member, under the object ledger: a `T{...}`
// temporary bound to a const reference parameter, and a local, each of an
// aggregate whose members' class has a constructor and a destructor - and a
// later member's constructor that throws, which must destroy the members
// already built ([except.ctor]/2) and never the one that did not finish.
extern "C" int printf(const char *, ...);
#include "lifetime.h"

struct Tracked {
    int v;
    Tracked(int x);
    Tracked(const Tracked &o);
    ~Tracked();
};
Tracked::Tracked(int x) : v(x) { if (x < 0) throw x; lfBuilt(this, "Tracked"); }
Tracked::Tracked(const Tracked &o) : v(o.v) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }

struct Agg {
    Tracked a;
    Tracked b;
    int c;
};

int take(const Agg &g) { return g.a.v + g.b.v + g.c; }

int main() {
    lfWatch();
    printf("%d\n", take(Agg{1, 2, 3}));
    try {
        printf("%d\n", take(Agg{4, -5, 6}));
    } catch (int e) {
        printf("caught %d\n", e);
    }
    Agg l = { 7, 8, 9 };
    printf("%d\n", l.a.v + l.b.v + l.c);
    try {
        Agg m = { 10, -11, 12 };
        printf("%d\n", m.c);
    } catch (int e) {
        printf("caught %d\n", e);
    }
    return 0;
}
