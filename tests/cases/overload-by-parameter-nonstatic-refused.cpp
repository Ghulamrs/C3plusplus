// A non-static member function named bare is no value, [expr.ref]/4: clang refuses `ap(h)` with
// "reference to non-static member function must be called", and cpp11 had read the global `h`.
int h(int x) { return x + 1; }
int ap(int (*g)(int)) { return g(3); }
struct X { int h(int) { return 50; } int go() { return ap(h); } };
int main() { X x; return x.go(); }
