// A class with a destructor returned from inside a handler: the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7); };
A::A(int x) : v(x) {}
struct D : A { D(); };
D::D() : A(3) {}

struct K { int v; K(int x); ~K(); };
K::K(int x) : v(x) {}
K::~K() {}
K f() { try { throw 1; } catch (int) { return K(4); } } int main() { printf("%d\n", f().v); }
