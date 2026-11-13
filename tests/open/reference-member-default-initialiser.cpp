// A reference member with a default member initialiser (C++11 [class.mem]): the parser asks for a
// constructor instead (R1's probes, 2026-11-13); the lowering's 'a member's default initialiser' sits behind it.
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

int g = 5; struct S { int &r = g; }; int main() { S s; printf("%d\n", s.r); }
