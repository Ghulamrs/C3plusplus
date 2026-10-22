// E : D : B, D adding a constructor of its own; D's own hides B's with the same parameters. clang: 1 2 30 2 100.
extern "C" int printf(const char *, ...);
struct B { int w; B(int) : w(1) {} B(double) : w(30) {} B(char, char) : w(100) {} };
struct D : B { using B::B; D(int) : B(0.0) { w = 2; } };
struct E : D { using D::D; };
int main() { B b(1); D d(1); D d2(2.5); E e(5); E e2('a', 'b'); printf("%d %d %d %d %d\n", b.w, d.w, d2.w, e.w, e2.w); return 0; }
