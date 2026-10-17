// A namespace member defined out of line, `int outer::f() { S s; }`, reads its parameters and
// its body in that namespace, [basic.lookup.unqual]/6 and /8 - and inside a namespace, whether
// written in its braces or not, a name of the namespace hides a global one of the same name.
extern "C" int printf(const char *, ...);

const int k = 100;
int counter = 1;
int helper() { return 1000; }
enum { Blue = 900 };

namespace outer {
    struct S { int v; S(); };
    const int k = 5;
    int counter = 2;
    enum Colour { Red = 2, Green = 3, Blue = 4 };
    int helper() { return 30; }
    int f();
    int g(S s);
    int braces() { return k + counter + Blue; }
    namespace inner { struct T { int w; T(); }; int calls = 0; int h(); int deep(); }
}
outer::S::S() : v(7) {}
outer::inner::T::T() : w(40) {}

int outer::f() { S s; return s.v + k + Green + helper() + counter; }
int outer::g(S s) { return s.v * 2 + Blue; }
int outer::inner::h() { T t; S s; return t.w + s.v + k; }
int outer::inner::deep() {
    struct L { int z; };
    L l = { 9 };
    calls++;   // outer::inner::calls
    return l.z + Red + calls;
}

int main() {
    outer::S s;
    int f = outer::f();
    int g = outer::g(s);
    int h = outer::inner::h();
    int d1 = outer::inner::deep();
    int d2 = outer::inner::deep();
    printf("%d %d %d %d %d %d\n", f, g, h, d1, d2, outer::braces());
    printf("%d %d\n", k + counter + helper() + Blue, &k == &outer::k);   // both kept: clang folds an unaddressed const
    return 0;
}
