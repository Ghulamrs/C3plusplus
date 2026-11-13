// A braced list over a bit-field and a reference member, then copied: the lowering's probe (R1, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} };
struct D : A { D() : A(3) {} };

struct S { int b : 3; int &r; }; int g = 2; int main() { S s = { 1, g }; S t(s); printf("%d %d\n", t.b, t.r); }
