// A constexpr constructor or member function of a class template, folded where a constant needs
// it: the specialization's body is compiled then, rather than after the file as every other
// member of a specialization is, which left `constexpr W<int> w(6);` with nothing to run.
extern "C" int printf(const char *, ...);

template <class T> struct W {
    T v;
    constexpr W(T a) : v(a) {}
    constexpr T get() const { return v; }
    constexpr W twice() const { return W(v * 2); }
};
template <class T> struct Pair {
    W<T> a, b;
    constexpr Pair(T x, T y) : a(x), b(y) {}
    constexpr T sum() const { return a.get() + b.get(); }
};
constexpr W<int> w(6);
constexpr Pair<double> pd(1.5, 2.25);
static_assert(w.get() == 6 && w.twice().v == 12, "W");
static_assert(pd.sum() == 3.75, "Pair");

int main() {
    constexpr Pair<int> pi(3, 4);
    static_assert(pi.sum() == 7 && pi.b.twice().get() == 8, "local");
    printf("%d %g %d\n", w.twice().get(), pd.sum(), pi.sum());
    return 0;
}
