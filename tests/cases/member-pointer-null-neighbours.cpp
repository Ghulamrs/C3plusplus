// The null data member pointer is -1 on Itanium wherever 0 or nullptr becomes one - assigned, returned, passed,
// compared - and a truth test asks for that null, on the C6000's walker as on SSA (lane c6m, 2026-11-14).
#include <cstdio>
struct S { int a, b; };
int S::*none() { return 0; }
int S::*some() { return &S::a; }
int test(int S::*p) { return p ? 1 : 0; }
int main() {
    int S::*p = &S::a; int S::*q = &S::b;
    p = nullptr; q = 0;
    printf("%d %d %d %d\n", p == 0, q == nullptr, 0 != p, (bool)q);
    printf("%d %d %d %d\n", test(0), test(nullptr), test(&S::a), !none());
    printf("%d %d %d\n", none() == 0, some() != 0, (bool)some());
    int n = 0;
    for (int S::*r = &S::a; r; r = none()) n++;
    do { p = 0; } while (p);
    if (!p && !q) n += 10;
    printf("%d\n", n);
    return 0;
}
