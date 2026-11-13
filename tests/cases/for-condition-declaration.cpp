// A declaration as a for's condition ([stmt.for]/1, [stmt.select]/2): made afresh each turn, the loop ends when it is zero (r1g, 2026-11-13).
#include <cstdio>
int main() { int n = 0; for (int i = 0; int j = 3 - i; ++i) n += j; printf("%d\n", n); }
