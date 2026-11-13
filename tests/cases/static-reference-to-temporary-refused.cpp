// A static local reference bound to a class temporary: [class.temporary]/5 would give the temporary static storage; refused by name, where it was destroyed at once (r1g, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x) : v(x) {} ~A() { printf("~A %d\n", v); } };
int f() { static const A &r = A(3); return r.v; }
int main() { printf("%d\n", f() + f()); }
