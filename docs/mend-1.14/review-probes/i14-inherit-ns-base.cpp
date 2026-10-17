// The base lives in a namespace. clang: 4 4.
extern "C" int printf(const char *, ...);
namespace ns { struct B { int a; B(int x) : a(x) {} }; }
struct D : ns::B { using ns::B::B; };
struct F : ns::B { using B::B; };
int main() { D d(4); F f(4); printf("%d %d\n", d.a, f.a); return 0; }
