// [expr.cond]/3: `c ? t : ct`, a T lvalue and a const T lvalue, is a const T lvalue - so it picks f(const T &) and
// the copy assignment, never f(T &&) or the move. cxx3's own C6xPipe.cpp and ParserTopLevel.cpp write the shape.
#include "lifetime.h"
struct T { int v; T(int x); T(const T &o); T(T &&o); ~T(); T &operator=(const T &o); T &operator=(T &&o); };
T::T(int x) : v(x) { lfBuilt(this, "T"); }
T::T(const T &o) : v(o.v) { lfBuilt(this, "T"); printf("copy-built\n"); }
T::T(T &&o) : v(o.v) { lfBuilt(this, "T"); printf("move-built\n"); }
T::~T() { lfGone(this, "T"); v = -1; }
T &T::operator=(const T &o) { v = o.v; printf("copy-assigned %d\n", v); return *this; }
T &T::operator=(T &&o) { v = o.v; printf("move-assigned %d\n", v); return *this; }
static int f(const T &t) { printf("f(const T &) %d\n", t.v); return 1; }
static int f(T &&t) { printf("f(T &&) %d\n", t.v); return 2; }
struct Box { T held; Box(); void put(const T &t); void put(T &&t); };
Box::Box() : held(0) {}
void Box::put(const T &t) { held = t; printf("put(const T &)\n"); }
void Box::put(T &&t) { held = static_cast<T &&>(t); printf("put(T &&)\n"); }
static int run(bool c, const T &ct) {
    T a(1);
    int r = f(c ? a : ct) * 10 + f(c ? ct : a);
    T x(9);
    x = c ? a : ct;
    Box b;
    b.put(c ? a : ct);
    const T &g = c ? a : ct;
    r = r * 10 + (&g == (c ? &a : &ct));
    return r * 100 + x.v * 10 + b.held.v;
}
int main() {
    lfWatch();
    const T ct(3);
    printf("%d\n", run(true, ct));
    printf("%d\n", run(false, ct));
    return 0;
}
