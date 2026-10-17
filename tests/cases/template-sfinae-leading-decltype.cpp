// Expression SFINAE (1.15): a function template whose signature depends on its parameters through
// an expression, which the Itanium name spells as the expression itself. Every name here was measured
// with clang (docs/mend-1.14/expression-sfinae-design.md, the probe this case is made from); names.sh
// holds them, run.sh that each call reaches the template it should.
// A leading decltype, where no function parameter is in scope: casts, literals, unresolved calls.
// decltype as a leading return type (the parameters are not in scope: only T, T() and globals).
struct S { int size() const { return 3; } static int count(); double v; };
int free1(int);
template <class T> decltype(T() + T()) h1(T) { return T(); }
template <class T> decltype(T()) h2(T) { return T(); }
template <class T> decltype(T::count()) h3(T) { return 0; }
template <class T> decltype(sizeof(T)) h4(T) { return 0; }
template <class T> decltype(free1(T())) h5(T) { return 0; }
template <class T> decltype(T().size()) h6(T) { return 0; }
template <class T> decltype(T().v) h7(T) { return 0; }
template <class T> decltype(*T()) h8(T t) { return *t; }
template <class T> decltype(T() < T()) h9(T) { return false; }
template <class T> decltype(T(1)) h10(T) { return T(1); }
template <class T> decltype(T(1, 2)) h11(T) { return T(1, 2); }
template <class T> decltype(T(1.5)) h12(T) { return T(1.5); }
template <class T> decltype(T('a')) h13(T) { return T('a'); }
template <class T> decltype(T(true)) h14(T) { return T(true); }
template <class T, class U> decltype(T() + U()) h15(T, U) { return 0; }
template <class T> decltype(sizeof(T) + 1) h16(T) { return 0; }
template <class T> decltype((T*)0) h17(T) { return 0; }
template <class T> decltype(T(1u)) h18(T) { return 0; }
template <class T> decltype(T(1l)) h19(T) { return 0; }
template <class T> decltype(T(1ll)) h20(T) { return 0; }
template <class T> decltype(T(1ul)) h21(T) { return 0; }
template <class T> decltype(T(1ull)) h22(T) { return 0; }
template <class T> decltype(T(1.5f)) h23(T) { return 0; }
template <class T> decltype(T(-1)) h24(T) { return 0; }
template <class T> decltype(T(1) + 2) h25(T) { return 0; }
struct P { int a, b; P(int x, int y); };
P::P(int x, int y) : a(x), b(y) {}
extern "C" int printf(const char *, ...);
int main() {
    int i = 2; S s; double d = 1; P p(1, 2);
    h1(i); h2(i); h3(s); h4(i); h5(i); h6(s); h7(s); h8(&i); h9(i); h10(i); h11(p); h12(i); h13(i); h14(i);
    h15(i, d); h16(i); h17(i); h18(i); h19(i); h20(i); h21(i); h22(i); h23(i); h24(i); h25(i);
    printf("%s\n", "template-sfinae-leading-decltype ran");
    return 0;
}

// Defined, so the program links; the signatures above only name them.
int S::count() { return 2; }
int free1(int x) { return x; }
