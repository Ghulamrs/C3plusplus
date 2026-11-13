// A static member through `->` of an expression: the pointer is evaluated, the member is the one object ([expr.ref]/4) (R1 round 4, 2026-11-13).
#include <cstdio>
struct S { static int m; int v; };
int S::m = 4;
int n = 0;
S *at(S *p) { n++; return p; }
int main() { S s; S *q[2] = { &s, &s }; q[1]->m = 5; at(&s)->m += 1; printf("%d %d\n", q[0]->m, n); return 0; }
