// A pointer to a data member is a scalar ([basic.types]/9), so it is a truth value ([conv.bool]) in `?:`, `!` and a
// cast to bool; the parser's requireScalar refuses it - stricter than the standard (R1 round 4, 2026-11-13).
#include <cstdio>
struct S { int a, b; };
int main() { int S::*p = &S::b; int S::*q = 0; printf("%d %d %d\n", p ? 1 : 0, !q, (bool)p); return 0; }
