// A class template inheriting from a non-dependent base, and from a base with a reference parameter. clang: 3 4 / 10 9.
extern "C" int printf(const char *, ...);
struct B { int a; B(int x) : a(x) {} B(int &r, int) : a(r) { r = 9; } };
template <class T> struct D : B { using B::B; T t; };
int main() { D<char> d(3); D<double> e(4); int r = 10; D<int> f(r, 0); printf("%d %d / %d %d\n", d.a, e.a, f.a, r); return 0; }
