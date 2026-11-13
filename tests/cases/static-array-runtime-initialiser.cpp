// A function-local static array whose list is not constant, [stmt.dcl]/4: built the first time, the tail zero (R1's probes, 2026-11-13).
#include <cstdio>
int n = 0;
int g() { return 4 + n++; }
int f() { static int a[3] = { g(), g() + 1 }; return a[0] * 100 + a[1] * 10 + a[2]; }
int main() { printf("%d %d\n", f(), f()); }
