// A class with a destructor returned from inside a handler: the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

struct K { int v; K(int x) : v(x) {} ~K() {} }; K f() { try { throw 1; } catch (int) { return K(4); } } int main() { printf("%d\n", f().v); }
