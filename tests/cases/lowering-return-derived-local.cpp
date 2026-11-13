// A function returning a base by value from a derived local (slicing): the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

A f(int k) { D d; A a(k); if (k) return d; return a; } int main() { printf("%d\n", f(1).v); }
