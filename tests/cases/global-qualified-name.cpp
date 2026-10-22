// [namespace.qual]: `::f(x)`, `::g` and `::n::f()` look up from the global namespace - no enclosing
// namespace, no local, no class member of the name is asked. The compiler's own OptIr.cpp writes
// `::reg(name)` (M1 L11, 2026-10-23).
extern "C" int printf(const char *, ...);
static int reg(int n) { return n * 2; }
int g = 5;
enum { Tag = 40 };
namespace n { int f() { return 70; } int g = 3; }
namespace q {
    int reg(int n) { return ::reg(n) + 1; }
    int g = 9;
    int read() { return ::g + g + ::n::g; }
    int Tag = 1;
    int tags() { return ::Tag + Tag; }
}
struct S {
    int g;
    static int reg(int n) { return n + 1000; }
    int both() const { return ::reg(g) + reg(g) + ::g; }
};
int main() {
    int g = 100;
    int (*p)(int) = ::reg;
    const S s = {6};
    const int a = q::reg(3), b = ::reg(4), c = q::read(), d = q::tags(), e = s.both(), f = p(7);
    printf("%d %d %d %d %d %d %d %d %d\n", a, b, c, d, e, f, ::g, g, ::n::f());
    return 0;
}
