// A thread_local reference at namespace scope is refused by the SSA lowering by name (R1 round 4, 2026-11-13).
#include <cstdio>
int g = 5;
thread_local int &tr = g;
int main() { tr = 6; printf("%d\n", g); return 0; }
