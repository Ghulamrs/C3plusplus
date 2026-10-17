// A member of a nested namespace defined inside the enclosing namespace's block with a relative
// qualifier, [namespace.memdef]/2: `int b::f()` written in `namespace a` defines `a::b::f`. Its
// body and a default added on the definition read their names in `a::b`, which hides `::k` and
// `a::k`. Before 1.16 this was "'b' is not a class", and once found the definition was keyed
// `a::a::b::f` - a second function - so a default it added was never seen at the call. A
// variable defined so, `int b::v = k + 100`, reads its initialiser in `a::b` too
// ([basic.lookup.unqual]/13); written `int a::b::v` at file scope it had read `::k`.
extern "C" int printf(const char *, ...);
int k = 1;
namespace a {
    int k = 2;
    namespace b {
        int k = 3;
        int f();
        int g(int);
        extern int v;
        struct S { int m(); };
        namespace c { int k = 4; int h(); }
    }
    int b::f() { return k; }
    int b::g(int x = k * 10) { return x; }
    int b::v = k + 100;
    int b::S::m() { return k + 1000; }
    int b::c::h() { return k; }
}
namespace a { namespace b { int f2(); } int b::f2() { return a::k + b::k; } }
int main() {
    a::b::S s;
    printf("%d %d %d %d\n", a::b::f(), a::b::g(), a::b::g(7), a::b::v);
    printf("%d %d %d\n", s.m(), a::b::c::h(), a::b::f2());
    return 0;
}
