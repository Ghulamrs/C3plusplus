// [except.handle]/16: a class caught by value is destroyed when a new exception leaves its handler - by the handler's
// cleanup, before the catch ends - and the new one is caught by value in turn. The ledger balances under -front=ssa
// (M3b.2); each throw names an object.
#include "lifetime.h"

struct E {
    int v;
    E(int n);
    E(const E &o);
    ~E();
};
E::E(int n) : v(n) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }

static void raise(int k) { E e(k); throw e; }

static int replaced(int k) {
    try {
        try { raise(k); } catch (E e) { E other(e.v + 1); throw other; }
    } catch (E e) {
        return e.v;
    }
    return -1;
}

int main() {
    lfWatch();
    printf("%d\n", replaced(7));
    return 0;
}
