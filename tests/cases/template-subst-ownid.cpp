// A class template made from the read under -templates=subst whose constructor names its own template under other arguments
// (Two<const A, B>, its partial, which names Two<A, B> back): made inside the class as the replay makes it, in either order.
// The partial's members are inline: cxx1 refuses their out-of-line definitions ("defined twice", owed).
extern "C" int printf(const char *, ...);

template <class A, class B> struct Two {
    A a;
    B b;
    Two(A x, B y);
    Two(const Two<const A, B> &o);
    int sum() const { return static_cast<int>(a) + static_cast<int>(b); }
};
template <class A, class B> Two<A, B>::Two(A x, B y) : a(x), b(y) {}
template <class A, class B> Two<A, B>::Two(const Two<const A, B> &o) : a(o.a), b(o.b) {}

template <class A, class B> struct Two<const A, B> {
    const A a;
    B b;
    Two(A x, B y) : a(x), b(y) {}
    Two(const Two<A, B> &o) : a(o.a), b(o.b) {}
    int sum() const { return 10 * static_cast<int>(a) + static_cast<int>(b); }
};

int main() {
    Two<int, char> p(3, 4);
    Two<const int, char> q(p);
    Two<int, char> r(q);
    printf("%d %d %d\n", p.sum(), q.sum(), r.sum());
    Two<const short, long long> s(5, 6);
    Two<short, long long> t(s);
    printf("%d %d\n", s.sum(), t.sum());
    return 0;
}
