// Expression SFINAE (1.15): a function template whose signature depends on its parameters through
// an expression, which the Itanium name spells as the expression itself. Every name here was measured
// with clang (docs/mend-1.14/expression-sfinae-design.md, the probe this case is made from); names.sh
// holds them, run.sh that each call reaches the template it should.
// The substitution table: a type operand is a candidate, the decltype a candidate as a whole.
// What the substitution table does with expression components: repeats of the same type, the same enable_if, a parameter in two places.
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
struct S { int size() const { return 1; } };
template <class T> typename enable_if<sizeof(T) == 4, T>::type s1(T, T) { return T(); }
template <class T> typename enable_if<sizeof(T) == 4, int>::type s2(typename enable_if<sizeof(T) == 4, T>::type) { return 0; }
template <class T> typename enable_if<sizeof(T) == 4, int>::type s3(T, typename enable_if<sizeof(T) == 4, int>::type) { return 0; }
template <class T> auto s4(T t, T u) -> decltype(t.size() + u.size()) { return 0; }
template <class T> auto s5(T t) -> decltype(t.size() + t.size()) { return 0; }
template <class T> decltype(T() + T()) s6(T, T) { return 0; }
template <class T> auto s7(T t, int) -> decltype(t.size()) { return 0; }
template <class T, class U> auto s8(T t, U u) -> decltype(u + t) { return 0; }
template <class T> auto s9(const T &t) -> decltype(t.size()) { return 0; }
template <class T> auto s10(T *t) -> decltype(t->size()) { return 0; }
template <class T> typename enable_if<sizeof(T) == 1, int>::type s11(T, S) { return 0; }
extern "C" int printf(const char *, ...);
int main() {
    int i = 0; S s; char c = 0;
    s1(i, i); s2<int>(i); s3(i, 1); s4(s, s); s5(s); s6(i, i); s7(s, 1); s8(i, 2.0); s9(s); s10(&s); s11(c, s);
    printf("%s\n", "template-sfinae-substitution-table ran");
    return 0;
}
