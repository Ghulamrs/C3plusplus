// A member function of a base that is not the first, called by its unqualified name inside a member of the
// derived class, [class.member.lookup]/1: refused as "'bump' was not declared - a prototype must come first",
// where `this->bump()` compiles. Found writing base-member-call-ssa (M3b.2), 2026-10-26.
struct A { int a; int getA() const { return a; } };
struct B { int b; void bump() { b += 10; } };
struct M : A, B { int f() { bump(); return getA() + b; } };
extern "C" int printf(const char *, ...);
int main() { M m; m.a = 2; m.b = 0; printf("%d\n", m.f()); return 0; }
