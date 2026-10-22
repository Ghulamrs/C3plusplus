// A template-id in a function template's signature whose arguments mention no parameter is a real
// class, not a pattern stand-in. Before 1.14, enable_if<2 + 2 == 4, int>::type was a hard error (the
// stand-in has no members), and with enable_if<true, int> made first the name came out
// _Z2m5IiEN9enable_ifILb1EiE4typeET_ where clang writes _Z2m5IiEiT_.
extern "C" int printf(const char *, ...);
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
enable_if<true, int>::type early = 5;
template <class T> typename enable_if<2 + 2 == 4, int>::type m1(T) { return 1; }
template <class T> typename enable_if<true, T>::type m2(T t) { return t + 1; }
template <class T> typename enable_if<true>::type m3(T) { }
template <class T> typename enable_if<true, int>::type m5(T) { return 5; }
template <int N> struct V { int a[N]; };
template <int N, int M> int cat(V<N>, V<M>) { V<N + M> r; return (int)sizeof(r.a) / (int)sizeof(int); }
int main() {
    V<1> a; V<2> b;
    m3(0);
    printf("%d %d %d %d %d\n", m1(1), m2(2), m5(3), early, cat(a, b));
    return 0;
}
