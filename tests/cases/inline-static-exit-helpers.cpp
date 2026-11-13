// An inline function's static local and its static reference's temporary, used from two units: cl makes each exit-time
// helper External in a COMDAT, so the link keeps one, and the ledger shows each object built and destroyed once.
#include "lifetime.h"
struct A { int v; A(int x); ~A(); };
A::A(int x) : v(x) { lfBuilt(this, "A"); }
A::~A() { lfGone(this, "A"); printf("~A %d\n", v); }
inline int h() { static const A &q = A(4); return q.v; }
inline int k() { static A a(5); return a.v; }
int use();
int use2();
int main() {
    lfWatch();
    const int a = h(), b = k(), c = use(), d = use2();
    printf("%d %d %d %d\n", a, b, c, d);
    return 0;
}
