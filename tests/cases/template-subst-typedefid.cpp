// A class template made from the read under -templates=subst whose member typedef names a class template-id over its
// parameters the program had not declared (Rev<T *>), beside a signature's (List<T>): both made first, in the body's order.
extern "C" int printf(const char *, ...);

template <class I> struct Rev {
    I it;
    Rev();
    explicit Rev(I i);
    I base() const { return it; }
};
template <class I> Rev<I>::Rev() : it(0) {}
template <class I> Rev<I>::Rev(I i) : it(i) {}

template <class T> struct List {
    const T *p;
    int n;
    List(const T *q, int m);
    int size() const { return n; }
};
template <class T> List<T>::List(const T *q, int m) : p(q), n(m) {}

template <class T> struct Vec {
    typedef T *iterator;
    typedef Rev<T *> reverse_iterator;
    T items[4];
    int count;
    Vec();
    void assign(List<T> l) { count = 0; for (int i = 0; i < l.size() && i < 4; i++) items[count++] = l.p[i]; }
    reverse_iterator rbegin() { return reverse_iterator(items + count); }
    T sum() const { T s = 0; for (int i = 0; i < count; i++) s += items[i]; return s; }
};
template <class T> Vec<T>::Vec() : count(0) {}

int main() {
    int a[3] = {4, 5, 6};
    Vec<int> v;
    v.assign(List<int>(a, 3));
    printf("%d %d %d\n", v.count, v.sum(), *(v.rbegin().base() - 1));
    double d[2] = {1.5, 2.25};
    Vec<double> w;
    w.assign(List<double>(d, 2));
    printf("%.2f %d\n", w.sum(), static_cast<int>(w.rbegin().base() - w.items));
    return 0;
}
