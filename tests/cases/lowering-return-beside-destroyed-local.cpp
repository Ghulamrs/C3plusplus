// A plain class returned through a pointer while a local with a destructor is alive: the lowering's probe (R1 round 2, 2026-11-13).
#include <cstdio>
struct L { ~L() { printf("~L\n"); } };
struct A { int v; };
A f(A *p) { L l; return *p; }
int main() { A a = { 3 }; printf("%d\n", f(&a).v); }
