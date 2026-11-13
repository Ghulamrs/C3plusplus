// `void (&r)() = *p;` with p a pointer to a function: [expr.unary.op]/1 makes `*p` the function lvalue, which binds the
// reference; the parser reads `*p` back as p itself and refuses the binding as a pointer (R1 round 4, 2026-11-13).
#include <cstdio>
void f() { printf("f\n"); }
int main() { void (*p)() = f; void (&r)() = *p; r(); return 0; }
