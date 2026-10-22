// A base's member template named by a qualified name in a derived member - `FlowOf<Entry>::build(...)`
// through a template-id, `B::build(...)` through a class - is called on the base subobject, and a
// derived member of the same name is not asked. It was refused through the template-id, and through
// a class it recursed until the stack ran out. The compiler's own OptFlow.h writes it (M1 L14).
extern "C" int printf(const char *, ...);
template <class E> struct FlowOf {
    int n;
    template <class F> void build(int k, F f) { n = f(k); }
    template <class F> static int apply(F f) { return f(2); }
};
struct Flow : FlowOf<int> { void build(int k, int c) { FlowOf<int>::build(k, [c](int i) { return i * c; }); } };
struct B { int m; template <class F> void build(int k, F f) { m = f(k) + 1; } };
struct D : B { void build(int k, int c) { B::build(k, [c](int i) { return i * c; }); } };
struct Pad { double d; };
struct Two : Pad, FlowOf<char> { void go() { FlowOf<char>::build(6, [](int i) { return i + 100; }); } };
int main() {
    Flow f;
    f.build(4, 3);
    D d;
    d.build(5, 3);
    Two t;
    t.d = 0;
    t.go();
    const int s = FlowOf<long>::apply([](int i) { return i * 7; });
    printf("%d %d %d %d\n", f.n, d.m, t.n, s);
    return 0;
}
