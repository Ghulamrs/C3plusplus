// operator-> returning a class by value, whose own operator-> gives the pointer ([over.ref]): cxx3 refuses the second hop (R1 round 2, 2026-11-13).
#include <cstdio>
struct S { int x; };
struct P1 { S *p; S *operator->() const { return p; } };
struct P2 { P1 q; P1 operator->() const { return q; } };
int main() { S s = { 9 }; P1 a = { &s }; P2 b = { a }; printf("%d\n", b->x); }
