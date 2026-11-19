// M8.2 2f: a type parameter bound to an lvalue reference to a scalar - `T &&` deduced from an lvalue is T = U &, the parameter
// collapsed to U & ([dcl.ref]/6) - made from the read under -templates=subst; std::forward and std::move over it.
#include <utility>
extern "C" int printf(const char *, ...);

template <class T> struct unref { typedef T type; };
template <class T> struct unref<T &> { typedef T type; };
template <class T> T &&fwd(typename unref<T>::type &a) { return static_cast<T &&>(a); }

template <class T> void inc(T &&a) { a += 1; }
template <class T> int twice(T &&a) { return a * 2; }
template <class T> T &&same(T &&a) { return a; }
template <class T> void setTo(T &&a, int v) { T b = a; b = v; }
template <class T> int through(T &&a) { return twice(fwd<T>(a)); }
template <class T> int viaStd(T &&a) { return twice(std::forward<T>(a)); }
template <class T> int moved(T &&a) { return twice(std::move(a)); }

int main() {
    int x = 3;
    double d = 1.5;
    const int c = 4;
    inc(x);
    inc(d);
    printf("%d %g\n", x, d);
    printf("%d %d\n", twice(x), twice(5));
    same(x) = 7;
    setTo(x, 9);
    printf("%d %d\n", x, twice(c));
    printf("%d %d %d %d\n", through(x), viaStd(x), viaStd(2), moved(x));
    return 0;
}
