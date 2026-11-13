// A thread_local array whose list is not constant: built on the thread's first pass (R1's probes, 2026-11-13).
#include <cstdio>
int n = 0;
int g() { return 4 + n++; }
int f() { thread_local int a[2][2] = { { g(), 2 }, { g() } }; return a[0][0] * 1000 + a[0][1] * 100 + a[1][0] * 10 + a[1][1]; }
int main() { printf("%d %d\n", f(), f()); }
