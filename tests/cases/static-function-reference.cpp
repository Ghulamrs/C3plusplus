// A static local reference to a function: no temporary is made ([dcl.init.ref]/5); the global holds the function's address (r1g, 2026-11-13).
#include <cstdio>
int g() { return 4; }
int f() { static int (&r)() = g; return r(); }
int main() { printf("%d\n", f()); }
