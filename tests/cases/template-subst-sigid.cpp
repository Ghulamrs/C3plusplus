// A class template made from the read under -templates=subst whose member functions' signatures name class template-ids
// over ids of its parameters (Wrap<Box<T> >, Pair<Box<T>, Wrap<T> >) the program had not declared: the inner ids made
// first, then the signature's, as the replay makes them reading the declaration.
extern "C" int printf(const char *, ...);

template <class T> struct Box {
    T v;
};
template <class T> struct Wrap {
    T w;
    int tag() const;
};
template <class T> int Wrap<T>::tag() const { return 3; }
template <class A, class B> struct Pair {
    A a;
    B b;
};

template <class T> struct Holder {
    T x;
    Wrap<Box<T> > wrapped() const;
    Pair<Box<T>, Wrap<T> > paired(const Wrap<Box<T> > &w) const;
};
template <class T> Wrap<Box<T> > Holder<T>::wrapped() const {
    Wrap<Box<T> > r;
    r.w.v = x;
    return r;
}
template <class T> Pair<Box<T>, Wrap<T> > Holder<T>::paired(const Wrap<Box<T> > &w) const {
    Pair<Box<T>, Wrap<T> > p;
    p.a.v = w.w.v;
    p.b.w = x + x;
    return p;
}

int main() {
    Holder<int> h;
    h.x = 5;
    Holder<long long> g;
    g.x = 40;
    const Pair<Box<int>, Wrap<int> > p = h.paired(h.wrapped());
    const Pair<Box<long long>, Wrap<long long> > q = g.paired(g.wrapped());
    printf("%d %d %d\n", h.wrapped().w.v, p.a.v * 100 + p.b.w, h.wrapped().tag());
    printf("%d %d\n", static_cast<int>(q.a.v), static_cast<int>(q.b.w));
    return 0;
}
