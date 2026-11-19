// M8.2 2d: a deduced call whose specialization takes or returns a reference, under -templates=subst - deduction through a
// forwarding reference and reference collapsing as the replay's, a named argument an lvalue, the call's result a reference.
extern "C" int printf(const char *, ...);

template <class T> struct unref { typedef T type; };
template <class T> struct unref<T &> { typedef T type; };
template <class T> typename unref<T>::type &&mv(T &&a) { return static_cast<typename unref<T>::type &&>(a); }
template <class T> void sw(T &a, T &b) { T t = mv(a); a = mv(b); b = mv(t); }
template <class T> T &pick(T &a) { return a; }
template <class T> T cref(const T &a) { return a * 3; }

template <class T> T useMv(T a) { T b = mv(a); return b + 1; }
template <class T> T usePick(T a) { pick(a) = a + 2; return a; }
template <class T> T useSw(T a, T b) { sw(a, b); return a - b; }
template <class T> T useCref(T a) { return cref(a); }

int main() {
    printf("%d %g\n", useMv(4), useMv(1.5));
    printf("%d %g\n", usePick(3), usePick(0.5));
    printf("%d %g\n", useSw(10, 3), useSw(1.5, 0.25));
    printf("%d %g\n", useCref(4), useCref(0.5));
    return 0;
}
