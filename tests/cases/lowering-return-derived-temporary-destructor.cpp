// A class with a destructor returned from a derived temporary (slicing): the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

struct K { int v; K(int x) : v(x) {} ~K() {} }; struct L : K { L() : K(8) {} }; K f() { return L(); } int main() { printf("%d\n", f().v); }
