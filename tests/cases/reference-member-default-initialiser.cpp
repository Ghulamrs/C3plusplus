// A reference member with a default member initialiser, [class.mem]/5: bound by the implicit constructor (R1's probes, 2026-11-13).
#include <cstdio>
int g = 5;
struct S { int &r = g; };
int main() { S s; g = 6; printf("%d\n", s.r); }
