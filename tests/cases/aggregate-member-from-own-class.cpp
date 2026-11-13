// A braced list whose value is an object of the member's own class initialises that member whole ([dcl.init.aggr]/11); braces elided otherwise (r1g, 2026-11-13).
#include <cstdio>
struct S { int a, b; };
struct T { S s; int c; };
int main() { S x = { 1, 2 }; T t = { x, 3 }; T u = { 4, 5, 6 }; printf("%d %d %d %d %d %d\n", t.s.a, t.s.b, t.c, u.s.a, u.s.b, u.c); }
