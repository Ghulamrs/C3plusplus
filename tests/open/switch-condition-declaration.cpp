// A declaration as a switch's condition ([stmt.select]/2): cxx3 fails to parse it, not refused by name (R1 round 2, 2026-11-13).
#include <cstdio>
int g() { return 2; }
int f() { switch (int k = g()) { case 2: return k * 5; default: return 0; } }
int main() { printf("%d\n", f()); }
