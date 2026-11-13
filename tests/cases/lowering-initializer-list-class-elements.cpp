// An initializer_list of a class with a constructor, its elements copied from derived objects: the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

#include <initializer_list>
int main() { D d1, d2; std::initializer_list<A> l = { d1, d2 }; printf("%d\n", (int)l.size() + l.begin()->v); }
