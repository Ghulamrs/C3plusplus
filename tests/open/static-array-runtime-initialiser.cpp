// A function-local static array initialised at run time: refused as 'expected a constant initialiser',
// not by name (R1's probes, 2026-11-13); the lowering's 'a static array built at run time' sits behind it.
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

int g() { return 4; } int f() { static int a[2] = { g(), g() + 1 }; return a[1]; } int main() { printf("%d\n", f()); }
