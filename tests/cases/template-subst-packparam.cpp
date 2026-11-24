// Classes over a parameter pack or a template template parameter they name in member bodies alone, under -templates=subst:
// made from the read, the bodies replayed; one that names its pack in a data member (Call) is replayed by name, as before.
extern "C" int printf(const char *, ...);

template <class T, class... Ts> struct Tagged {
    T v;
    T next() const { return v + 1; }
};

template <class F> struct Traits {
    int k;
};
template <class R, class... A> struct Traits<R(A...)> {
    R r;
    int k;
    R both() const { return r + k; }
};

template <class U> struct Box {
    U u;
};
template <template <class> class C, class T> struct Holder {
    T x;
    T twice() const { return x * 2; }
};

template <int... Ns> struct Seq {
    int n;
};

template <template <class> class C> struct Maker {
    int make() const { C<int> c; c.u = 4; return c.u; }
};

template <class... Ts> struct Call {
    Tagged<int, Ts...> t;
};

template <class... Ts> struct Count {
    int size() const { return sizeof...(Ts); }
};

int main() {
    Tagged<int, char, double> a = { 6 };
    Tagged<char> b = { 'a' };
    Traits<int(char, double)> c = { 3, 4 };
    Traits<int> d = { 9 };
    Holder<Box, int> e = { 21 };
    Seq<1, 2, 3> f = { 5 };
    Seq<> g = { 8 };
    Count<int, char, short> h;
    printf("%d %c %d %d %d %d %d %d\n", a.next(), b.next(), c.both(), d.k, e.twice(), f.n, g.n, h.size());
    printf("%d %d %d\n", (int)sizeof(Tagged<int, char, double>), (int)sizeof(Traits<int(char, double)>), (int)sizeof(Holder<Box, int>));
    Call<char, short> i = { { 2 } };
    Maker<Box> m;
    printf("%d %d\n", i.t.v, m.make());
    return 0;
}
