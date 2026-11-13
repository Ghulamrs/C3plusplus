// A pointer to a data member is a scalar ([basic.types]/9), so it is a truth value ([conv.bool]) in `?:`, `!`, a cast
// to bool, `&&`, `||` and a condition; the null one is -1 on Itanium, cl's own on Microsoft (R1 round 5, 2026-11-13).
#include <cstdio>
struct S { int a, b; };
struct B { int x; }; struct C { int y; }; struct D : B, C { int z; };
int main() {
    int S::*p = &S::b; int S::*q = 0; int S::*z = &S::a;
    printf("%d %d %d %d %d\n", p ? 1 : 0, !q, (bool)p, (bool)q, z ? 1 : 0);
    printf("%d %d %d\n", p && z, q || z, !!q);
    int D::*m = &D::z; int D::*n = 0; int C::*y = &C::y; int D::*c = y;
    if (m) printf("m\n");
    if (!n) printf("!n\n");
    while (c) { printf("c\n"); c = 0; }
    return 0;
}
