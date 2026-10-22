// Expression SFINAE (1.15): a function template whose signature depends on its parameters through
// an expression, which the Itanium name spells as the expression itself. Every name here was measured
// with clang (docs/mend-1.14/expression-sfinae-design.md, the probe this case is made from); names.sh
// holds them, run.sh that each call reaches the template it should.
// `T::count()` and `T::value`: `sr T_ <name>`, with T pushed once as the type it is.
// `sr T_`: a qualified name through a template parameter, and what it does to the substitution table.
struct S { static int count(); static const int value = 3; int size() const { return 1; } };
template <class T, class U> auto q1(T t, U u) -> decltype(T::count() + u) { return 0; }
template <class T, class U> auto q3(T t, U u) -> decltype(T::value + u) { return 0; }
template <class T> auto q4(T t) -> decltype(t.size() + T::value) { return 0; }
template <class T> auto q5(T t) -> decltype(+t) { return +t; }
template <class T> auto q6(T t) -> decltype(t.size() == T::count()) { return true; }
extern "C" int printf(const char *, ...);
int main() { S s; int i = 0; q1(s, i); q3(s, i); q4(s); q5(i); q6(s); printf("%s\n", "template-sfinae-unresolved ran"); return 0; }

// Defined, so the program links; the signatures above only name them.
int S::count() { return 2; }
