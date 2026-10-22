// [expr.prim.lambda]/15: a by-reference capture refers to the entity itself, so a const object or a
// const reference is captured as `const T &`. The closure's member dropped the const and could not
// bind - the compiler's own OptFlow.h and Lexer.cpp met it (M1 L7, 2026-10-23).
extern "C" int printf(const char *, ...);
struct S { int v; int get() const { return v; } };
static int viaParam(const S &t) { auto f = [&t]() { return t.get(); }; return f(); }
static int viaDefault(const S &t, const int k) { auto f = [&]() { return t.v * k; }; return f(); }
int main() {
    const int c = 4;
    const S s = {6};
    auto a = [&]() { return c + s.v; };
    auto b = [&c, &s]() { return c * s.get(); };
    int n = 1;
    auto w = [&n]() { n = 7; };
    w();
    const int x = a(), y = b(), z = viaParam(s), q = viaDefault(s, 5);
    printf("%d %d %d %d %d\n", x, y, z, q, n);
    int m = 2;
    auto outer = [&]() { auto inner = [&]() { m += c; return c * 10; }; return inner() + m; };
    const int o = outer();
    printf("%d %d\n", o, m);
    return 0;
}
