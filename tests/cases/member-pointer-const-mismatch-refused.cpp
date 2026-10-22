// A const member's address does not convert to a pointer to a non-const one: two types.
struct S { int f() const { return 1; } };
int main() { int (S::*p)() = &S::f; S s; return (s.*p)(); }
