// A member function of a class in a namespace, defined outside the namespace block:
// its body is in the class's scope, whose enclosing namespace is outer. clang: 37 37 5.
extern "C" int printf(const char *, ...);
int k = 131;
namespace outer {
    int k = 37;
    enum { E = 5 };
    struct C { int f(); int g(); int h(); };
}
int outer::C::f() { return k; }
int outer::C::g() { return outer::k; }
int outer::C::h() { return E; }
int main() { outer::C c; printf("%d %d %d\n", c.f(), c.g(), c.h()); return 0; }
