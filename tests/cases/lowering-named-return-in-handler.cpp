// A named return value declared and returned inside a handler: the lowering's probe (R1 round 2, 2026-11-13).
#include <cstdio>
struct A { int v; A(int x) : v(x) {} A(const A &o) : v(o.v) {} ~A() {} };
A f(int k) { try { if (k) throw 1; } catch (int) { A a(3); a.v++; return a; } return A(1); }
int main() { printf("%d %d\n", f(0).v, f(1).v); }
