// A reference to a function bound to a '?:' of two functions (an lvalue of function type, [expr.cond]/4): it holds the chosen function's address (r1g, 2026-11-13).
#include <cstdio>
int g() { return 1; } int h() { return 2; }
int f(int k) { int (&r)() = k ? g : h; return r(); }
int main() { printf("%d %d\n", f(0), f(1)); }
