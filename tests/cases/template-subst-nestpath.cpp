// M8.3 slice 3v: a nested class read by the class body's path, as the replay reads it - a data member over the parameter, its
// constructors, a body naming the enclosing class's static - its class made from the read with no replay under -templates=subst.
extern "C" int printf(const char *, ...);

template <class X> struct Holder {
    struct Box { X held; Box(X h) : held(h) {} X twice() const { return held + held; } };
};
template <class T, int N> struct Scaled {
    static int scale(int n) { return n * N; }
    struct In { T base; In() : base(1) {} int get() { return Scaled::scale(2) + static_cast<int>(base); } };
};
template <class T> struct Tag { struct type { int k; type() : k(1) {} type(int x) : k(x) {} }; };
template <class It> int kOf(It t) { return typename Tag<It>::type(t).k; }
template <class T> struct Outer {
    static T base() { return T(2); }
    struct Mid { T v; Mid() : v(Outer::base()) {} struct Deep { static T twice(T x) { return x + x; } }; T both() { return Deep::twice(v); } };
};

int main() {
    Holder<int>::Box b(4);
    Holder<double>::Box d(1.25);
    Scaled<int, 3>::In in;
    Scaled<char, 5>::In c;
    printf("%d %g\n", b.twice(), d.twice());
    printf("%d %d\n", in.get(), c.get());
    printf("%d %d\n", kOf(9), Tag<long long>::type().k);
    Outer<int>::Mid m;
    printf("%d %g\n", m.both(), Outer<double>::Mid::Deep::twice(1.5));
    return 0;
}
