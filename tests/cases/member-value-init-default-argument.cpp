// `: a()` over a default constructor whose parameter is defaulted, [dcl.fct.default]/4: called with 7 (R1's miscompile, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7); };
A::A(int x) : v(x) {}
struct B { A a; B(); };
B::B() : a() {}
int main() { B b; printf("%d\n", b.a.v); }
