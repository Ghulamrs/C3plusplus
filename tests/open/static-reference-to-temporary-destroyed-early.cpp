// A static local reference bound to a class temporary: the temporary lives to the end of the program ([class.temporary]/5); cxx3 destroys it at once, before main's printf (R1 round 2, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x) : v(x) {} ~A() { printf("~A %d\n", v); } };
int f() { static const A &r = A(3); return r.v; }
int main() { printf("%d\n", f() + f()); }
