// A named return value built by a constructor's defaults, and one from a braced list: the lowering's probe (R1 round 2, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x = 7) : v(x) {} A(const A &o) : v(o.v) {} ~A() {} };
A f() { A a; a.v++; return a; }
A g() { A a{3}; a.v++; return a; }
int main() { printf("%d %d\n", f().v, g().v); }
