// `(c ? a : P()).set(5)`: the conditional is a prvalue ([expr.cond]/6), so an lvalue arm is copied and the copy is what
// changes - a class copied as bytes, one with a copy constructor, one with a destructor and a byte copy, one moved from.
#define LF_BALANCE_ONLY
#include "lifetime.h"

struct P { int n; void set(int v) { n = v; } };
struct Q { int n; Q(int v); Q(const Q &o); ~Q(); void set(int v) { n = v; } };
Q::Q(int v) : n(v) { lfBuilt(this, "Q"); }
Q::Q(const Q &o) : n(o.n) { lfBuilt(this, "Q"); }
Q::~Q() { lfGone(this, "Q"); }
struct R { int n; ~R(); void set(int v) { n = v; } };
R::~R() { printf("~R %d\n", n); }
struct M { int n; M(int v); M(const M &o); M(M &&o); ~M(); };
M::M(int v) : n(v) { lfBuilt(this, "M"); }
M::M(const M &o) : n(o.n) { lfBuilt(this, "M"); }
M::M(M &&o) : n(o.n) { o.n = 0; lfBuilt(this, "M"); }
M::~M() { lfGone(this, "M"); }

void bytes(bool c) {
    P a = { 1 };
    (c ? a : P()).set(5);
    printf("P %d\n", a.n);
    const P k = { 2 };
    printf("P read %d\n", (c ? k : P()).n);
}

void copied(bool c) {
    Q q(3);
    (c ? q : Q(4)).set(6);
    printf("Q %d\n", q.n);
    printf("Q read %d\n", (c ? q : Q(4)).n);
}

void destroyed(bool c) {
    R r = { 7 };
    if (c) (c ? r : R()).set(8);
    printf("R %d\n", r.n);
}

void moved(bool c) {
    M m(9);
    M made = c ? m : M(10);
    printf("M %d made %d\n", m.n, made.n);
}

int main(int argc, char **) {
    lfWatch();
    const bool yes = argc > 0, no = argc < 0;
    bytes(yes);
    bytes(no);
    copied(yes);
    copied(no);
    destroyed(yes);
    moved(yes);
    moved(no);
    return 0;
}
