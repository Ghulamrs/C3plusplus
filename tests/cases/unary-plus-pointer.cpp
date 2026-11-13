// Unary + of a pointer and of an array is the pointer's value: [expr.unary.op]/7 (R1 round 4, 2026-11-13).
#include <cstdio>
int main() { int a[2] = { 1, 2 }; int *p = a; int *q = +p; int *r = +a; printf("%d %d\n", *(q + 1), *r); return 0; }
