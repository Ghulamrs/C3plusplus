// A braced temporary the SSA lowering left to the walker, under the object
// ledger: `H{v, T(..)}` of a class with a constructor of its own, bound to a
// const reference parameter - once with the argument's constructor throwing.
#define LF_BALANCE_ONLY
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

struct Holder {
    int v;
    Tracked t;
    Holder(int x, const Tracked &o);
    ~Holder();
};
Holder::Holder(int x, const Tracked &o) : v(x), t(o) {}
Holder::~Holder() {}

int useH(const Holder &h) { return h.v + h.t.v; }

int main() {
    lfWatch();
    printf("%d\n", useH(Holder{1, Tracked(2)}));
    try {
        printf("%d\n", useH(Holder{9, Tracked(-10)}));
    } catch (int e) {
        printf("caught %d\n", e);
    }
    return 0;
}
