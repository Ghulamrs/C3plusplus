// A thread_local array initialised at run time: refused as 'expected a constant initialiser', not by name
// (R1's probes, 2026-11-13); the lowering's 'a thread_local array built at run time' sits behind it.
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

int g() { return 4; } int f() { thread_local int a[2] = { g(), 2 }; return a[0]; } int main() { printf("%d\n", f()); }
