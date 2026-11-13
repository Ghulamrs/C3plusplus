// [class.temporary]/5: a static reference's temporary has static storage of its own - built where the reference is bound,
// destroyed at exit - at namespace scope (a call's result too, of a class with no destructor), in a namespace, as a static member and as a static local, class and scalar.
#include "lifetime.h"
static int watched = (lfWatch(), 0);    // registered first, so the ledger reports after every temporary is gone
struct A { int v; A(int x); ~A(); };
A::A(int x) : v(x) { lfBuilt(this, "A"); }
A::~A() { printf("~A %d\n", v); lfGone(this, "A"); }
struct P { int a, b; };
P two(int x) { P p = { x, x * 10 }; return p; }
int g() { return 8; }
static const A &gr = A(1);
const A &xr = A(2);
const P &pq = two(2);
namespace ns { const A &nr = A(5); }
struct S { static const A &m; };
const A &S::m = A(6);
const int &ir = 7;
const int &jr = g();
const P &pr = P();
int f() { static const A &r = A(3); return r.v; }
inline int h() { static const A &q = A(4); return q.v; }
int k() { static const int &s = g() + 1; return s; }
int main() {
    printf("%d %d %d %d %d %d %d %d %d\n", gr.v, xr.v, ns::nr.v, S::m.v, ir, jr, pr.a + pr.b, pq.a + pq.b, watched);
    const int a = f(), b = h();      // built in this order, so destroyed in the other
    printf("%d %d %d %d %d\n", a + f(), b + h(), k(), k(), f());
    return 0;
}
