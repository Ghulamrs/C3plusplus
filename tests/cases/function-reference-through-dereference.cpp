// `void (&r)() = *p;` with p a pointer to a function: [expr.unary.op]/1 makes `*p` the function lvalue, which binds the
// reference (R1 round 5, 2026-11-13).
#include <cstdio>
void f() { printf("f\n"); }
int g(int a, int b) { return a * 10 + b; }
int main() {
    void (*p)() = f; void (&r)() = *p; r();
    int (*q)(int, int) = g; int (&s)(int, int) = **q; printf("%d\n", s(4, 2));
    return 0;
}
