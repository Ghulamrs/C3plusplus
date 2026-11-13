// A static local built by a constructor whose argument is defaulted: the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

int f() { static A a; return a.v; } int main() { printf("%d\n", f()); }
