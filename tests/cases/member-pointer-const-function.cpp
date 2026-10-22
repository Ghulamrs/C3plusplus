// A pointer to a const member function, `int (S::*)() const`, is a type of its own - its function type
// carries the const `this` as it carries a ref-qualifier - spelled `M1SKFivE` and `P8S@@EBAHXZ`, both
// measured. Refused by name until the compiler's own Parser.h needed it (M1 L9, 2026-10-23).
extern "C" int printf(const char *, ...);
struct S { int v; int f() const { return v + 1; } int g() { return v + 2; } int h(int k) const & { return v + k; } int f2(int k) const { return k * 2; } int f2(int k) { return k * 3; } };
static int take(int (S::*p)() const, const S &s) { return (s.*p)(); }
static int take2(int (S::*p)(int) const &, const S *s) { return (s->*p)(4); }
static int take3(int (S::*p)(), S &s) { return (s.*p)(); }
int main() {
    S s = {10};
    int (S::*pc)(int) const = &S::f2;
    int (S::*pn)(int) = &S::f2;
    printf("%d %d %d %d %d\n", take(&S::f, s), take2(&S::h, &s), take3(&S::g, s), (s.*pc)(5), (s.*pn)(5));
    return 0;
}
