// [expr.call]/1: a base's operator named by its qualified name in a member of a derived class -
// `B::operator=(o)` - calls that one, without dispatch. For a base whose copy assignment is the
// compiler's and trivial there is no function, and the base subobject is assigned by its bytes.
// The compiler's own C6xModel.h writes `LineFields::operator=(o)` (M1 L8, 2026-10-23).
#include <utility>
extern "C" int printf(const char *, ...);
struct B { int v; };
struct W {
    int v;
    W &operator=(const W &o) { v = o.v + 1; return *this; }
    W &operator=(W &&o) { v = o.v + 100; return *this; }
    int operator+(int k) const { return v + k; }
};
struct Pad { double d; };
struct D : B { int k; D &operator=(const D &o) { B::operator=(o); k = 1; return *this; } };
struct E : W {
    int k;
    E &operator=(const E &o) { W::operator=(o); k = 2; return *this; }
    E &operator=(E &&o) { W::operator=(std::move(o)); k = 3; return *this; }
    int plus(int n) const { return W::operator+(n); }
};
struct F : Pad, B {
    int k;
    F &operator=(const F &o) { B &self = B::operator=(o); self.v += 1000; k = 4; return *this; }
};
int main() {
    D a, b; b.v = 7; b.k = 0; a = b;
    E c, d; d.v = 7; d.k = 0; c = d;
    E e; e = std::move(d);
    F f, g; g.d = 1.5; g.v = 9; f.d = 0; f = g;
    printf("%d %d | %d %d | %d %d %d | %d %g %d\n", a.v, a.k, c.v, c.k, e.v, e.k, e.plus(5), f.v, f.d, f.k);
    return 0;
}
