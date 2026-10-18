// The lookups an inline namespace has to answer for, each measured against clang:
//   - unqualified, from inside the enclosing namespace: `use()` in lib sees both f's,
//     so `f(1)` takes v2's `f(int)` and `f(1.5)` lib's own `f(double)` - one overload set
//   - `using namespace lib;` opens the inline namespace's members too
//   - argument-dependent lookup in both directions ([basic.lookup.argdep]/2: a class in
//     the inline namespace has the enclosing one as an associated namespace, and a class
//     in the enclosing one has the inline one): `tag2(t)` finds lib's for a v2::T,
//     `tag(u)` finds v3's for a lib::U
//   - a variable, a typedef and an enumerator through the enclosing name
//   - an out-of-line definition through the enclosing name: `int lib::g(int)` defines
//     lib::v2::g, which its prototype declared there.
extern "C" { int printf(const char *, ...); }
namespace lib {
    inline namespace v2 {
        struct T { int v; };
        int f(int) { return 1; }
        int tag(T) { return 7; }
        int g(int);
        typedef int Count;
        enum Colour { Red = 3, Green };
        int counter = 9;
    }
    int f(double) { return 2; }
    int tag2(T t) { return t.v + 100; }
    struct U { int v; };
    inline namespace v3 { int tag(U) { return 8; } }
    int use() { return f(1) * 10 + f(1.5); }
    int viaEnclosing() { return counter + Green; }
}
int lib::g(int x) { return x + 1; }
int main() {
    lib::T t = { 5 };
    lib::U u = { 2 };
    lib::Count n = 5;
    lib::Colour k = lib::Green;
    printf("%d %d %d %d %d\n", lib::f(1), lib::f(2.5), tag(t), tag(u), lib::use());
    printf("%d %d %d %d\n", tag2(t), lib::g(1), lib::v2::g(2), lib::viaEnclosing());
    printf("%d %d %d %d\n", n, (int)k, lib::counter, (int)lib::Red);
    {
        using namespace lib;
        printf("%d %d %d\n", f(3), f(0.5), (int)Green);
    }
    return 0;
}
