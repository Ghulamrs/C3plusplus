// A static local reference to a function: no temporary is made ([dcl.init.ref]/5), cxx3 refuses it as binding one (R1 round 2, 2026-11-13).
#include <cstdio>
int g() { return 4; }
int f() { static int (&r)() = g; return r(); }
int main() { printf("%d\n", f()); }
