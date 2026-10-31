// [except.handle]/3 and /16: a class caught by value is copy-initialised from the exception object - its copy constructor
// run before the catch begins - and destroyed on every way out of the handler before the catch ends: falling off it, a
// return, a break and a rethrow. The ledger balances under -front=ssa (M3b.2); each throw names an object.
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

struct Bytes { int a, b; Bytes(int x); };
Bytes::Bytes(int x) : a(x), b(x * 3) {}

static void raise(int k) { E e(k); throw e; }

static int fallOff(int k) {
    int got = 0;
    try { raise(k); } catch (E e) { got = e.v; e.v = 0; }
    return got;
}

static int returned(int k) {
    try { raise(k); } catch (const E e) { return e.v * 2; }
    return -1;
}

static int broken(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        try { raise(i); } catch (E e) { if (e.v == 2) break; sum += e.v; }
    }
    return sum;
}

static int rethrown(int k) {
    try {
        try { raise(k); } catch (E e) { e.v = 99; throw; }
    } catch (E &outer) {
        return outer.v;
    }
    return -1;
}

static int bytes(int k) {
    try { Bytes b(k); throw b; } catch (Bytes c) { return c.a + c.b; }
    return -1;
}

int main() {
    lfWatch();
    printf("%d %d\n", fallOff(3), returned(4));
    printf("%d\n", broken(5));
    printf("%d\n", rethrown(6));
    printf("%d\n", bytes(5));
    return 0;
}
