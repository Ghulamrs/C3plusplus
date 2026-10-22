// A const object calls through a pointer to a const member only, [expr.mptr.oper]/6.
struct S { int g() { return 1; } };
int main() { int (S::*p)() = &S::g; const S s = S(); return (s.*p)(); }
