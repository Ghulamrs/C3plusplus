// A lambda reads its enclosing function's static local uncaptured: [expr.prim.lambda]/10 captures only automatic objects (R1 round 4, 2026-11-13).
#include <cstdio>
int f() { static int s = 3; auto l = []() { return s + 1; }; s = 5; return l(); }
int main() { printf("%d\n", f()); return 0; }
