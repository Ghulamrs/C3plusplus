// A constructor's function try block, [except.pre]/4: its handler covers the mem-initialisers as
// well as the body. What was built is destroyed before the handler runs, [except.handle]/14, and
// flowing off the handler's end rethrows, /15 - a handler may throw its own instead. And a
// destructor's, which covers the members it destroys after its body. C++98; cpp11 refused both
// until 2026-10-20.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
int limit = 100;
struct M { int v; M(int x); ~M(); };
M::M(int x) : v(x) { if (v >= limit) throw v; lfBuilt(this, "M"); printf("+M%d ", v); }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
struct B { B(); ~B(); };
B::B() { lfBuilt(this, "B"); printf("+B "); }
B::~B() { printf("-B "); lfGone(this, "B"); }
struct S : B {
    M a, b;
    S(int k);
};
S::S(int k) try : a(1), b(k) {
    printf("body ");
    if (k == 3) throw 30;
} catch (int n) {
    printf("handler%d ", n);
}
struct T {
    M m;
    T(int k) try : m(k) { printf("t-body "); } catch (...) { printf("t-handler "); throw 77; }
};
struct U { M m; int k; U(int x); ~U(); };
U::U(int x) : m(x), k(x) {}
U::~U() try { printf("~U%d ", k); } catch (...) { printf("never "); }
template <class F> void run(const char *what, int lim, F f) {
    limit = lim;
    printf("%s: ", what);
    try { f(); } catch (int n) { printf("caught%d ", n); }
    printf("\n");
    limit = 100;
}
static void ok() { S s(2); }
static void member() { S s(9); }
static void body() { S s(3); }
static void own() { T t(5); }
static void destructor() { U a(11); U b(12); }
int main() {
    lfWatch();
    run("ok", 100, ok);
    run("a member throws", 9, member);
    run("the body throws", 100, body);
    run("a handler throws its own", 5, own);
    run("destructor", 100, destructor);
    return 0;
}
