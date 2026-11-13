// A member copied from a derived object by the implicit copy constructor: overload resolution finds no
// A::A for the one argument (R1's probes, 2026-11-13); the lowering's 'a member copied from another type' sits behind it.
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

struct S { A a; S(const D &d) : a(d) {} }; int main() { D d; S s(d); printf("%d\n", s.a.v); }
