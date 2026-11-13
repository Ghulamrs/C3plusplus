// A static thread_local reference in a block is refused by the SSA lowering by name (R1 round 4, 2026-11-13).
#include <cstdio>
int g = 5;
int f() { static thread_local int &r = g; return r; }
int main() { printf("%d\n", f()); return 0; }
