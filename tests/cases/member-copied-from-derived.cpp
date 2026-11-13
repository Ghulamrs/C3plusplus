// A member copied from a derived object, [class.copy]/12: its A subobject's bytes (R1's probes, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7); };
A::A(int x) : v(x) {}
struct D : A { int w; D(); };
D::D() : A(3), w(9) {}
struct S { A a; S(const D &d); };
S::S(const D &d) : a(d) {}
int main() { D d; S s(d); printf("%d\n", s.a.v); }
