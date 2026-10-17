// Copy-initialisation, braces, new with braces, and an argument, through inherited constructors. clang: 5 6 7 8 9.
extern "C" int printf(const char *, ...);
struct B { int a; B(int x) : a(x) {} B(int x, int y) : a(x + y) {} };
struct D : B { using B::B; };
int take(D d) { return d.a; }
int main() { D d = 5; D e{6}; D *p = new D{3, 4}; D f = {8}; printf("%d %d %d %d %d\n", d.a, e.a, p->a, f.a, take(9)); delete p; return 0; }
