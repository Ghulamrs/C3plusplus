// A declaration as a for's condition ([stmt.for]/1, [stmt.select]/2): cxx3 fails to parse it, not refused by name (R1 round 2, 2026-11-13).
#include <cstdio>
int main() { int n = 0; for (int i = 0; int j = 3 - i; ++i) n += j; printf("%d\n", n); }
