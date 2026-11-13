// A for's condition declaration of an unsigned, with a break, and one in a for with no init or step ([stmt.for]/1) (r1g, 2026-11-13).
#include <cstdio>
int main() { int n = 0; for (int i = 10; unsigned j = i % 4; i++) { if (i == 99) break; n += j; } int m = 0; for (; int k = m < 3; ) m += k; printf("%d %d\n", n, m); }
