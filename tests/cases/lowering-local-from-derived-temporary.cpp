// A base local copy-initialised from a derived temporary (slicing): the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

int main() { A a = D(); printf("%d\n", a.v); }
