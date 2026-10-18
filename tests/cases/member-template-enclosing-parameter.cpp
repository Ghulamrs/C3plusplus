// A member template's signature may compute with the enclosing class template's parameter -
// `CVector<N + K> operator|(const CVector<K> &) const` in a CCS sample. In `CVector<3>` the class's N
// is bound to 3, and clang spells it as that value: `V<N + K>` is `S_IXplLm3ET_EE`, measured. cpp11
// refused the signature by name from 1.15 to 2026-10-18 for want of that measurement.
extern "C" int printf(const char *, ...);
typedef decltype(sizeof 0) size_t;
template <size_t N> struct V {
    double v[N];
    static int size() { return (int)N; }
    void fill() { for (size_t i = 0; i < N; i++) v[i] = (double)(i + 1); }
    template <size_t K> V<N + K> operator|(const V<K> &rhs) const {
        V<N + K> out = V<N + K>();
        for (size_t i = 0; i < N; i++) out.v[i] = v[i];
        for (size_t i = 0; i < K; i++) out.v[N + i] = rhs.v[i] * 10;
        return out;
    }
    template <size_t K> V<N * K> spread(const V<K> &) const { V<N * K> r = V<N * K>(); return r; }
    template <class T> V<N + sizeof(T)> grow(T) const { V<N + sizeof(T)> r = V<N + sizeof(T)>(); return r; }
};
template <int A> struct W {
    template <int B> W<A - B> sub(W<B>) const { return W<A - B>(); }
    static int value() { return A; }
};
int main() {
    V<3> a; V<2> b; a.fill(); b.fill();
    V<5> c = a | b;
    printf("%g %g %g %g %g\n", c.v[0], c.v[1], c.v[2], c.v[3], c.v[4]);
    V<6> s = a.spread(b);
    V<4> g = a.grow('x');
    W<5> w = W<7>().sub(W<2>());
    printf("%d %d %d %g\n", s.size(), g.size(), w.value(), s.v[0] + g.v[3]);
    return 0;
}
