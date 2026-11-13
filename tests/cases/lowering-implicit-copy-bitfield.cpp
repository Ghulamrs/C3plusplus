// The implicit copy constructor of a class with a bit-field and a member with a copy of its own: the lowering's probe (R1 round 2, 2026-11-13).
#include <cstdio>
struct M { int v; M(int x) : v(x) {} M(const M &o) : v(o.v + 1) {} };
struct S { M m; unsigned b : 3; S() : m(1), b(5) {} };
int main() { S s; S t(s); printf("%d %u\n", t.m.v, t.b); }
