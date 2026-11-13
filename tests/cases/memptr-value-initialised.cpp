// A pointer to a data member value-initialised, `P()`, is the null member pointer: [expr.type.conv]/2 (R1 round 4, 2026-11-13).
#include <cstdio>
struct S { int a, b; };
typedef int S::*P;
int main() { P p = P(); P q = &S::b; S s = { 1, 2 }; printf("%d %d\n", p == 0, s.*q); return 0; }
