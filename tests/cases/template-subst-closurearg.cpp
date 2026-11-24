// M8.3: a class template whose type parameter is bound to a lambda's closure type, made from the read under -templates=subst.
extern "C" int printf(const char *, ...);

template <class A, class B> struct Same { enum { value = 0 }; };
template <class A> struct Same<A, A> { enum { value = 1 }; };
template <class T> struct Unref { typedef T type; };
template <class T> struct Unref<T &> { typedef T type; };
template <class F> struct Holder {
    F f;
    int extra;
    int call(int n) const { return f(n) + extra; }
    int size() const { return static_cast<int>(sizeof(F)); }
};
template <class F> struct Model {
    static int call(void *p, int n) { return (*static_cast<F *>(p))(n); }
};

int main() {
    int k = 3, m = 10;
    auto twice = [](int n) { return 2 * n; };
    auto plus = [k](int n) { return n + k; };
    auto both = [k, m](int n) { return n * k + m; };
    Holder<decltype(plus)> h = { plus, 100 };
    printf("%d %d\n", h.call(4), h.size() == static_cast<int>(sizeof(int)));
    Holder<decltype(both)> g = { both, 1 };
    printf("%d %d\n", g.call(2), g.size() == static_cast<int>(2 * sizeof(int)));
    printf("%d %d %d\n", Model<decltype(twice)>::call(&twice, 21), Model<decltype(plus)>::call(&plus, 1), Model<decltype(both)>::call(&both, 1));
    printf("%d %d\n", (int)Same<decltype(plus), decltype(plus)>::value, (int)Same<decltype(plus), decltype(both)>::value);
    Unref<decltype(both) &>::type copy = both;
    printf("%d\n", copy(5));
    return 0;
}
