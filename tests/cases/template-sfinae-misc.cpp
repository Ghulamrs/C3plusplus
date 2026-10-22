// Expression SFINAE (1.15): a function template whose signature depends on its parameters through
// an expression, which the Itanium name spells as the expression itself. Every name here was measured
// with clang (docs/mend-1.14/expression-sfinae-design.md, the probe this case is made from); names.sh
// holds them, run.sh that each call reaches the template it should.
// Non-dependent operands, defaulted arguments, a non-type parameter, pointers and references.
// Non-dependent expressions in a template signature, defaulted enable_if, a non-type parameter in decltype,
// a class-template argument that is an expression, and a pointer/reference to a dependent member type.
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <int N> struct Box { int a[N]; };
struct S { int size() const { return 1; } };
template <class T> typename enable_if<2 + 2 == 4, int>::type m1(T) { return 1; }
template <class T> typename enable_if<true, T>::type m2(T) { return T(); }
template <class T> typename enable_if<sizeof(T) == 4>::type m3(T) {}
template <class T> auto m4(T t) -> decltype(1 + 2) { return 3; }
template <class T, int N> auto m5(T t) -> decltype(t + N) { return t + N; }
template <class T, int N> auto m6(T t) -> decltype(N) { return N; }
template <class T> void m7(T, Box<sizeof(T)>) {}
template <class T> void m8(T, Box<(sizeof(T) > 2 ? 1 : 2)>) {}
template <class T> typename enable_if<sizeof(T) == 4, int>::type *m9(T) { return 0; }
template <class T> typename enable_if<sizeof(T) == 4, int>::type &m10(T, int &r) { return r; }
template <class T> void m12(T, typename enable_if<sizeof(T) == 4, int>::type = 0) {}
template <class T> int m13(T, typename enable_if<sizeof(T) == 4>::type * = 0) { return 0; }
template <class T> auto m14(T t) -> decltype(t.size() + sizeof(T)) { return 0; }
template <class T> auto m16(T t) -> typename enable_if<sizeof(T) == 4, T>::type { return t; }
template <class T, class U> auto m17(T t, U u) -> decltype(t.size() + u.size()) { return 0; }
template <class T> auto m18(T *t) -> decltype(t[0]) { return t[0]; }
template <class T> auto m19(T t) -> decltype(t.size() * 2u) { return 0; }
template <class T> auto m20(T t) -> decltype(t.size() * 2ll) { return 0; }
template <class T> auto m21(T t) -> decltype(t.size() + 1.5) { return 0; }
template <class T> auto m22(T t) -> decltype(t.size() == 1 ? t : t) { return t; }
template <class T> auto m23(T t) -> decltype(S().size()) { return 0; }
template <class T> auto m24(T t) -> decltype(sizeof(t) == sizeof(T)) { return true; }
template <class T> auto m25(T t) -> decltype(static_cast<T>(1)) { return T(); }
template <class T> auto m26(T t) -> decltype(T(1) + T(2)) { return T(); }
template <class T> auto m28(T t) -> decltype(-sizeof(T)) { return 0; }
template <class T> auto m29(T t) -> decltype(t.size() < 2 && sizeof(T) > 2) { return true; }
template <class T> auto m30(T t) -> decltype(sizeof(T) - 1 - 2) { return 0; }
template <class T> auto m31(T t) -> decltype(sizeof(T) - (1 - 2)) { return 0; }
template <class T> auto m32(T t) -> decltype(t.size() - t.size()) { return 0; }
extern "C" int printf(const char *, ...);
int main() {
    int i = 0; S s; Box<4> b4; Box<1> b1; int r = 0;
    m1(i); m2(i); m3(i); m4(i); m5<int, 2>(i); m6<int, 2>(i); m7(i, b4); m8(i, b1); m9(i); m10(i, r); m12(i);
    m13(i); m14(s); m16(i); m17(s, s); m18(&i); m19(s); m20(s); m21(s); m22(s); m23(i); m24(i); m25(i); m26(i);
    m28(i); m29(s); m30(i); m31(i); m32(s);
    printf("%s\n", "template-sfinae-misc ran");
    return 0;
}
