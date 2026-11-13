// A reference member to a base bound to a derived object: the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

struct S { const A &r; S(const D &d) : r(d) {} }; int main() { D d; S s(d); printf("%d\n", s.r.v); }
