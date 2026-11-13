// A class local copy-initialised through another class's conversion function: the lowering's probe (R1 round 2, 2026-11-13).
#include <cstdio>
struct B { int v; };
struct A { int k; operator B() const { B b = { k * 2 }; return b; } };
int main() { A a = { 4 }; B b = a; printf("%d\n", b.v); }
