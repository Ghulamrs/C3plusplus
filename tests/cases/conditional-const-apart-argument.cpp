// [expr.cond]/3: `c ? t : ct`, a T lvalue and a const T lvalue, is a const T lvalue - bound to a reference parameter
// it names the arm taken, and no copy is made. cppparse's X86Module::codeFor passes `any ? folded : f` so.
#include "lifetime.h"
struct F { int v; F(int x); F(const F &o); ~F(); };
F::F(int x) : v(x) { lfBuilt(this, "F"); }
F::F(const F &o) : v(o.v) { lfBuilt(this, "F"); printf("copied\n"); }
F::~F() { lfGone(this, "F"); v = -1; }
struct H { const F &r; explicit H(const F &f); };
H::H(const F &f) : r(f) {}
static int same(const F &a, const F &b) { return &a == &b; }
static int value(F f) { return f.v; }
int run(const F &f, bool any) {
    F folded(7);
    H h(any ? folded : f);
    int r = h.r.v * 10 + same(h.r, any ? folded : f) + same(any ? f : folded, f) * 2;
    const F &g = (any ? folded : f);
    F c = any ? f : folded;
    return (r * 10 + (&g == &folded)) * 10 + value(any ? folded : f) + c.v;
}
int main() {
    lfWatch();
    F f(3);
    printf("%d %d\n", run(f, true), run(f, false));
    return 0;
}
