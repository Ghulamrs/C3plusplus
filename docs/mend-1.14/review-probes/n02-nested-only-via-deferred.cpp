// A nested class's inline member called only from a nested-nested body, itself called only from the outer's. clang: 21.
extern "C" int printf(const char *, ...);
template <class T> struct O {
    struct A { int f() const { return 7; } struct B { int g(const A &a) const { return a.f() * 3; } }; };
    int run() const { A a; typename A::B b; return b.g(a); }
};
int main() { O<int> o; printf("%d\n", o.run()); return 0; }
