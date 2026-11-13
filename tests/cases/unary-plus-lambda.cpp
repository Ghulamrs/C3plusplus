// Unary + of a captureless lambda is its conversion to a pointer to function: [expr.prim.lambda]/6 (R1 round 4, 2026-11-13).
#include <cstdio>
int main() { int (*fp)(int) = +[](int x) { return x + 1; }; printf("%d\n", fp(4)); return 0; }
