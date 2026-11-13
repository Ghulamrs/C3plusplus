// operator-> returning a class by value, whose own operator-> gives the pointer ([over.ref]); `P2 b = { a }` initialises the member q whole from a P1, no braces elided into it ([dcl.init.aggr]/11) (r1g, 2026-11-13).
#include <cstdio>
struct S { int x; };
struct P1 { S *p; S *operator->() const { return p; } };
struct P2 { P1 q; P1 operator->() const { return q; } };
int main() { S s = { 9 }; P1 a = { &s }; P2 b = { a }; printf("%d\n", b->x); }
