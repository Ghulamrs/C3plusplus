// Two bases, each inherited; the other base default-constructed. clang: 1 20 / 10 2.
extern "C" int printf(const char *, ...);
struct A { int a; A() : a(10) {} A(int x) : a(x) {} };
struct B { int b; B() : b(20) {} B(const char *) : b(2) {} };
struct D : A, B { using A::A; using B::B; };
int main() { D x(1); D y("s"); printf("%d %d / %d %d\n", x.a, x.b, y.a, y.b); return 0; }
