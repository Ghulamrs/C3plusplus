// A class template made from the read under -templates=subst with a type parameter bound to a reference: Iter's R is
// `int &`, `const int &` or `Pt &`, collapsed under `R &` as the replay collapses it. Replayed by name: Peek<R>'s `const R`
// (the replay keeps the qualifier) and Box<T> with T = `int &` (a data member made a reference).
extern "C" int printf(const char *, ...);

struct Pt {
    int x, y;
};

template <class T, class R, class P> struct Iter {
    P p_;
    explicit Iter(P q);
    R operator*() const;
    R &same(R r) const;
    Iter &next();
    bool done(P end) const;
};
template <class T, class R, class P> Iter<T, R, P>::Iter(P q) : p_(q) {}
template <class T, class R, class P> R Iter<T, R, P>::operator*() const { return *p_; }
template <class T, class R, class P> R &Iter<T, R, P>::same(R r) const { return r; }
template <class T, class R, class P> Iter<T, R, P> &Iter<T, R, P>::next() {
    ++p_;
    return *this;
}
template <class T, class R, class P> bool Iter<T, R, P>::done(P end) const { return p_ == end; }

template <class T> struct Box {
    T v_;
    explicit Box(T v);
    T get() const;
};
template <class T> Box<T>::Box(T v) : v_(v) {}
template <class T> T Box<T>::get() const { return v_; }

template <class R> struct Peek {
    const R pick(R r) const;
};
template <class R> const R Peek<R>::pick(R r) const { return r; }

int main() {
    int a[4] = {3, 5, 7, 9};
    int sum = 0;
    for (Iter<int, int &, int *> i(a); !i.done(a + 4); i.next()) {
        *i += 1;
        sum += *i;
    }
    Iter<int, const int &, const int *> c(a);
    int &r = Iter<int, int &, int *>(a).same(a[2]);
    r = 40;
    Pt pts[2] = {{1, 2}, {3, 4}};
    Iter<Pt, Pt &, Pt *> p(pts);
    (*p).x = 10;
    p.next();
    (*p).y += (*p).x;
    int n = 6;
    Box<int &> b(n);
    b.get() = 11;
    Peek<int &>().pick(n) += 1;
    Box<int> v(12);
    printf("%d %d %d %d\n", sum, *c, *Iter<int, const int &, const int *>(a + 2), a[3]);
    printf("%d %d %d %d\n", pts[0].x, pts[1].y, n, v.get());
    return 0;
}
