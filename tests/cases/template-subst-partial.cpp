// A member type of a class template-id with partial specializations nothing declared before the class: under -templates=subst
// the door makes Traits<int *> first from the partial the replay picks (T *, not const T *), and Rev<int *> from the read.
extern "C" int printf(const char *, ...);

template <class I> struct Traits { typedef typename I::value value; typedef typename I::ref ref; };
template <class T> struct Traits<T *> { typedef T value; typedef T &ref; };
template <class T> struct Traits<const T *> { typedef T value; typedef const T &ref; };

template <class I> struct Rev {
    typedef typename Traits<I>::value value;
    typedef typename Traits<I>::ref ref;
    I it;
    ref operator*() const { I t = it; --t; return *t; }
    value twice() const { return **this * 2; }
};

int main() {
    int a[3] = {4, 5, 6};
    Rev<int *> r;
    r.it = a + 3;
    *r = 9;
    printf("%d %d %d\n", a[2], *r, r.twice());
    const double d[2] = {1.5, 2.5};
    Rev<const double *> c;
    c.it = d + 2;
    printf("%.1f %.1f\n", *c, c.twice());
    return 0;
}
