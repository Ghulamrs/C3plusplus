// [namespace.def]/8 and [over.match]: a name looked up through the enclosing namespace -
// qualified, unqualified from inside it, or through a using-directive - gathers the
// overloads of the inline namespace and of the enclosing one into one set, and the
// ordinary ranking chooses. Each overload returns a number of its own, so identical
// output means identical choices.
extern "C" { int printf(const char *, ...); }
namespace lib {
    inline namespace v2 {
        int f(int x)    { return 1 + 0 * x; }
        int f(char x)   { return 2 + 0 * x; }
    }
    int f(double x)     { return 3 + 0 * (int)x; }
    int f(const char *) { return 4; }
    int inside() { return f(1) * 1000 + f('c') * 100 + f(2.5) * 10 + f("s"); }
}
int main(void) {
    using namespace lib;
    printf("%d %d %d %d\n", lib::f(1), lib::f('c'), lib::f(2.5), lib::f("s"));
    printf("%d %d %d %d\n", f(1), f('c'), f(2.5), f("s"));
    printf("%d %d %d\n", lib::inside(), lib::v2::f(1), lib::f(1.0f));
    return 0;
}
