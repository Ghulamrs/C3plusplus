// [basic.lookup.qual]/4: `::X` inside `namespace n` names the global X and never `n::X`, as a type,
// a functional cast, a typedef, a static member and a template argument; cxx3's own Templates.cpp
// writes `::Local(...)` inside `namespace sema` (lane m6d-glob, 2026-11-19).
extern "C" int printf(const char *, ...);
#include <vector>
#include "lifetime.h"

struct X {
    int v;
    X(int a);
    X(const X &o);
    ~X();
    static int count;
};
X::X(int a) : v(a) { lfBuilt(this, "X"); }
X::X(const X &o) : v(o.v) { lfBuilt(this, "X"); }
X::~X() { lfGone(this, "X"); }
int X::count = 40;

namespace n {
struct X {
    int w, u;
    X(int a, int b);
    static int count;
};
X::X(int a, int b) : w(a), u(b) {}
int X::count = 2;

int sum() {
    ::X a(1);
    const int b = ::X(2).v;
    typedef ::X G;
    G c(3);
    std::vector< ::X> vs;
    vs.push_back(::X(4));
    std::vector< ::X>::value_type d(5);
    const X mine(6, 7);
    return a.v + b + c.v + vs[0].v + d.v + mine.w + mine.u;
}
int counts() { return ::X::count * 10 + X::count; }
}

int main() {
    lfWatch();
    printf("%d %d %d %d\n", n::sum(), X::count, n::X::count, n::counts());
    return 0;
}
