// A MISCOMPILE: a member value-initialised by a() whose constructor's argument is defaulted prints an
// uninitialised value at -O0 and -O2 on x86_64-linux; clang prints 7 (R1's probes, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

struct B { A a; B() : a() {} }; int main() { B b; printf("%d\n", b.a.v); }
