// A class template over a non-type parameter made from the read under -templates=subst: its own id over its parameters in
// order (`V<N>`, `Box<T, N>`) named in return types, out of line too, and a parameter: the read class itself.
extern "C" int printf(const char *, ...);

template <int N> struct V {
    int a[N];
    V<N> &fill(int k);
    V<N> twice() const { V<N> r; for (int i = 0; i < N; i++) r.a[i] = 2 * a[i]; return r; }
    int sum() const { int t = 0; for (int i = 0; i < N; i++) t += a[i]; return t; }
    bool same(const V<N> &o) const { return sum() == o.sum(); }
};
template <int N> V<N> &V<N>::fill(int k) { for (int i = 0; i < N; i++) a[i] = k + i; return *this; }

template <class T, int N> struct Box {
    T b[N];
    Box<T, N> &set(T v) { for (int i = 0; i < N; i++) b[i] = v; return *this; }
    T first() const { return b[0]; }
};

int main() {
    V<3> v;
    v.fill(1);
    V<3> w = v.twice();
    Box<double, 2> b;
    b.set(2.5);
    printf("%d %d %d %d\n", v.sum(), w.sum(), static_cast<int>(v.same(w)), static_cast<int>(sizeof(V<3>)));
    printf("%d %d\n", static_cast<int>(b.first() * 2), static_cast<int>(sizeof(Box<double, 2>)));
    return 0;
}
