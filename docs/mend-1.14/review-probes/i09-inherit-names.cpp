extern "C" int printf(const char *, ...);
struct B { int a; B(int x); B(int &r, double d); };
B::B(int x) : a(x) {}
B::B(int &r, double d) : a(r + (int)d) {}
struct D : B { using B::B; int m; };
struct E : D { using D::D; };
int main() { int r = 1; D d(1); E e(r, 2.0); printf("%d %d\n", d.a, e.a); return 0; }
