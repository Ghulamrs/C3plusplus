// A static member through `.` of a call or a parenthesis is refused by the SSA lowering by name (R1 round 4, 2026-11-13).
#include <cstdio>
struct S { static int m; int v; };
int S::m = 4;
S make() { S s; s.v = 1; return s; }
int main() { S s; printf("%d %d\n", make().m, (s).m); return 0; }
