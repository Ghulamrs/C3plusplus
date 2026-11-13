// `++` of a bool sets it true, deprecated and still C++11: [expr.post.incr]/1, [expr.pre.incr]/1 (R1 round 4, 2026-11-13).
#include <cstdio>
int main() { bool b = false; b++; bool c = b++; bool d = false; ++d; printf("%d %d %d\n", (int)b, (int)c, (int)d); return 0; }
