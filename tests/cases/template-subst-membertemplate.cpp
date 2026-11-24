// A class template with member templates made from the read under -templates=subst: Rev<int *>'s converting constructor
// template and Box<int>'s member function template are declared under the class's binding as its replay declares them.
extern "C" int printf(const char *, ...);

template <class I> struct Rev {
    I it;
    Rev() : it(0) {}
    explicit Rev(I i) : it(i) {}
    template <class U> Rev(const Rev<U> &o) : it(o.it) {}
    I base() const { return it; }
    int at() const { I t = it; --t; return *t; }
};

template <class T> struct Box {
    T v;
    template <class U> T plus(U u) const { return v + static_cast<T>(u); }
    template <class U> void set(const U &u) { v = static_cast<T>(u); }
    T get() const { return v; }
};

int main() {
    int a[3] = {4, 5, 6};
    Rev<int *> r(a + 3);
    Rev<const int *> c(r);
    printf("%d %d %d\n", r.at(), c.at(), static_cast<int>(c.base() - a));
    Box<int> b;
    b.set(2.75);
    printf("%d %d %d\n", b.get(), b.plus(3), b.plus(1.5));
    Box<double> d;
    d.set(4);
    printf("%.2f %.2f\n", d.get(), d.plus(1));
    return 0;
}
