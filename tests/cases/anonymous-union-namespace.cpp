// [class.union]/6: an anonymous union at namespace scope is `static` - or in
// an unnamed namespace, which gives the same linkage - and its members are
// names of that scope sharing one object. The storage's name is measured
// from clang: the first member, mangled as a variable with no `L` (`_Z1a`,
// `_ZN1n1eE`), and on Microsoft `?$S1@@3T<unnamed-type-$S1>@@A`.
extern "C" int printf(const char *, ...);

static union { int a; double b; };
namespace { union { int c; double d; }; }
namespace n {
    static union { int e; double f; };
    int read() { return e; }
}

int main() {
    a = 1;
    printf("%d %d\n", a, (int)((void *)&a == (void *)&b));
    b = 2.5;
    printf("%g\n", b);
    c = 3;
    printf("%d %d\n", c, (int)((void *)&c == (void *)&d));
    n::e = 4;
    printf("%d %d\n", n::read(), (int)sizeof(n::f));
    n::f = 0.5;
    printf("%g %d\n", n::f, (int)sizeof(a));
    return 0;
}
