// A default argument of the inherited constructor is evaluated in B's scope. clang: 11 3 13.
extern "C" int printf(const char *, ...);
enum { N = 100 };
struct B { enum { N = 10 }; static int s; int a; B(int x, int y = N) : a(x + y) {} B(double d, int z = s) : a((int)d + z) {} };
int B::s = 11;
struct D : B { using B::B; };
int main() { D d(1); D e(2.0); D f(3, 0); printf("%d %d %d\n", d.a, e.a, f.a); return 0; }
