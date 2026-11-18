// M8.1, the review's M4: valid shapes common in a template body that the read at
// the definition declines today, each in a template of its own so each is one
// unread body under tests/subst-cases.ceiling by its reason. The replay at the
// instantiation compiles them all; clang takes them all (it warns that two
// expression statements are unused).
#include <cstdio>
struct R {
    int v[2];
    const int *begin() const { return v; }
    const int *end() const { return v + 2; }
    static int s;
    static int f(int k) { return k; }
};
int R::s = 1;
int g(int v) { return v; }
template <class T, int N> int f1(T x, int k) { int t = 0; for (auto e : x) t += g(e); return t + k; }
template <class T, int N> int f2(const T &x, int k) { int t = 0; for (auto e : x) t += g(e); return t + k; }
template <class T, int N> int f3(T, int k) { T::s; return k; }
template <class T, int N> int f4(T, int k) { T::f(k); return k; }
template <class T, int N> int f5(T, int k) { T{}; return k; }
template <class T, int N> int f7(T, int k) { int (T::*pm) = 0; return k + (pm == 0); }
template <class T, int N> int f8(T, int k) { struct L { T m; }; L l; l.m.s = 1; return k; }
template <class T, int N> int f9(T, int k) { return k + static_cast<int>(alignof(T)); }
template <class T, int N> int f10(T x, int k) { return k + (dynamic_cast<T *>(&x) != 0); }
template <class T, int N> int f11(T, int k) { int a[N]; a[0] = k; return a[0]; }
template <class T, int N> int f12(T, int k) { char b[sizeof(T)]; b[0] = 1; return k + b[0]; }
int main() {
    R r = {{1, 2}};
    std::printf("%d %d %d %d %d\n", f1<R, 1>(r, 0), f2<R, 1>(r, 0), f3<R, 1>(r, 3), f4<R, 1>(r, 4), f5<R, 1>(r, 5));
    std::printf("%d %d %d %d %d %d\n", f7<R, 1>(r, 0), f8<R, 1>(r, 8), f9<R, 1>(r, 0), f10<R, 1>(r, 0),
                f11<R, 1>(r, 11), f12<R, 1>(r, 0));
    return 0;
}
