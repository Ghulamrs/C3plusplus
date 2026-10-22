// The derived class has a vptr, members with initialisers and a second base: all set by the inherited ctor.
extern "C" int printf(const char *, ...);
struct Other { int o; Other() : o(77) {} };
struct B { int a; B(int x) : a(x) {} virtual int f() { return 1; } virtual ~B() {} };
struct D : Other, B { using B::B; int m = 42; int n; virtual int f() { return 2; } };
int main() { D d(5); B *p = &d; printf("%d %d %d %d\n", d.a, d.o, d.m, p->f()); delete new D(1); return 0; }
