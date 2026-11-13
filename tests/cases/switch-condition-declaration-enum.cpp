// A switch's condition declaring an enumeration, read in a case ([stmt.select]/2) (r1g, 2026-11-13).
#include <cstdio>
enum E { A, B, C };
E pick(int i) { return i ? C : A; }
int f(int i) { int r = 0; switch (E e = pick(i)) { case A: r = 1; break; case C: r = 3 + e; break; default: r = -1; } return r; }
int main() { printf("%d %d\n", f(0), f(1)); }
