// [expr.call]/1: `V<I>::operator * (s)` in a member of a class derived from V<4> calls the base's
// member on this, never dispatched - as `Base::f(...)` with a plain name. cpp11 took a template-id
// qualifier for a static call and refused it (a CCS sample's CQuat).
extern "C" int printf(const char *, ...);
template <int N> struct V {
    double v[N];
    V operator*(double s) const { V r; for (int i = 0; i < N; i++) r.v[i] = v[i] * s; return r; }
    V operator-() const { V r; for (int i = 0; i < N; i++) r.v[i] = -v[i]; return r; }
    virtual double first() const { return v[0]; }
    double sum() const { double t = 0; for (int i = 0; i < N; i++) t += v[i]; return t; }
};
template <int I> struct Q : V<I> {
    Q() {}
    Q(const V<I> &b) : V<I>(b) {}
    Q operator*(double s) const { return Q(V<I>::operator * (s)); }
    Q operator-() const { return Q(V<I>::operator - ()); }
    double first() const { return 100 + V<I>::first(); }
    double total() const { return V<I>::sum(); }
};
int main() {
    Q<3> q; q.v[0] = 1; q.v[1] = 2; q.v[2] = 3;
    Q<3> d = q * 2.0, n = -q;
    V<3> &asBase = q;
    printf("%g %g %g %g %g\n", d.v[2], n.v[1], q.first(), asBase.first(), q.total());
}
