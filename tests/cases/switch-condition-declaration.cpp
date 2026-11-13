// A declaration as a switch's condition ([stmt.select]/2): its value is the switch's, and the name is in scope in the body (r1g, 2026-11-13).
#include <cstdio>
int g() { return 2; }
int f() { switch (int k = g()) { case 2: return k * 5; default: return 0; } }
int main() { printf("%d\n", f()); }
