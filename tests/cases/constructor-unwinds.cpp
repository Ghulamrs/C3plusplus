// [except.ctor]/2-3: an exception leaving a constructor destroys what it had built - its bases and
// members finished so far, last first, whether the throw came from a mem-initialiser or the body;
// an array's elements built so far; a virtual base the complete object's constructor built; and a
// delegating constructor's whole object once its target has returned. In a constructor the program
// writes and in the ones the compiler writes. cpp11 destroyed none of it until 2026-10-20 - every
// line below printed the same and the ledger alone said so.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
int limit = 100;
int serial = 0;
struct M { int v; M(); M(int x); M(const M &o); M(M &&o); ~M(); void check(); };
M::M() : v(++serial) { check(); }
M::M(int x) : v(x) { check(); }
M::M(const M &o) : v(o.v + 10) { check(); }
M::M(M &&o) : v(o.v + 20) { check(); }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
void M::check() { if (v >= limit) throw v; lfBuilt(this, "M"); printf("+M%d ", v); }
struct B { B(); ~B(); };
B::B() { lfBuilt(this, "B"); printf("+B "); }
B::~B() { printf("-B "); lfGone(this, "B"); }
struct Written : B { M a, b; Written(int k); };
Written::Written(int k) : a(1), b(k) { printf("body "); if (k == 3) throw k; }
struct Member { M bm; };
struct Implicit : Member { M a; M b; };
struct V { M vm; V(); };
V::V() : vm(7) {}
struct D1 : virtual V { M d; D1(); };
D1::D1() : d(8) {}
struct Del { M a; M b; Del(int k); Del(); };
Del::Del(int k) : a(k), b(k + 1) {}
Del::Del() : Del(2) { printf("deleg "); if (limit == 50) throw 50; }
struct Arr { M ms[4]; };
template <class F> void run(const char *what, int lim, F f) {
    limit = lim;
    serial = 0;
    printf("%s: ", what);
    try { f(); } catch (int n) { printf("caught%d ", n); }
    printf("\n");
    limit = 100;
}
static void writtenOk() { Written w(2); }
static void writtenInit() { Written w(9); }
static void writtenBody() { Written w(3); }
static void implicitDefault() { Implicit t; }
static void implicitCopy() { serial = 0; limit = 100; Implicit src; limit = 13; Implicit c(src); }
static void implicitMove() { serial = 0; limit = 100; Implicit src; limit = 23; Implicit c(static_cast<Implicit &&>(src)); }
static void virtualBase() { D1 d; }
static void delegating() { Del d; }
static void arrayMember() { Arr a; }
static void localArray() { M a[4]; }
static void newArray() { M *p = new M[4]; delete[] p; }
static void listArray() { M a[3] = { M(1), M(2), M(99) }; }
int main() {
    lfWatch();
    run("written", 100, writtenOk);
    run("written, a member throws", 9, writtenInit);
    run("written, the body throws", 100, writtenBody);
    run("implicit default", 3, implicitDefault);
    run("implicit copy", 13, implicitCopy);
    run("implicit move", 23, implicitMove);
    run("virtual base", 8, virtualBase);
    run("delegating", 50, delegating);
    run("delegating, the target throws", 3, delegating);
    run("array member", 3, arrayMember);
    run("local array", 3, localArray);
    run("new array", 3, newArray);
    run("braced list", 50, listArray);
    return 0;
}
