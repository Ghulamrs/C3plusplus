// M8.2 2f: a C-style cast, a functional cast and a static_cast to a non-reference type under -templates=subst - each the
// conversion the replay makes to the type the binding gives T; a cast to a pointer and `T()` replay by name.
extern "C" int printf(const char *, ...);

template <class T> T cstyle(double d) { return (T)d; }
template <class T> T functional(double d) { return T(d) + T(1); }
template <class T> T named(int i) { return static_cast<T>(i) / 2; }
template <class T> int toInt(T a) { return (int)a + static_cast<int>(a); }
template <class T> int discard(T a) { (void)a; return 1; }
template <class T> int pointer(T a) { const char *p = (const char *)&a; return p != 0; }
template <class T> T zero() { return T(); }

int main() {
    printf("%d %g\n", cstyle<int>(3.75), cstyle<double>(3.75));
    printf("%d %g\n", functional<int>(2.5), functional<double>(2.5));
    printf("%d %g\n", named<int>(7), named<double>(7));
    printf("%d %d %d\n", toInt(2.9), toInt(5), toInt('A'));
    printf("%d %d %d\n", discard(4), pointer(3), zero<int>());
    return 0;
}
