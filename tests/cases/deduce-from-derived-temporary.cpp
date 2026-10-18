// [temp.deduct.call]/4.3 with [dcl.init.ref]/5: `near1(q1, Q<>(1, 0, 0, 0))` deduces N from the Q<4>
// temporary's V<4> base and binds the const reference to it - the CCS samples' `nearly(q, CQuat<>(...))`,
// which deduced and then found no viable binding for the temporary.
extern "C" int printf(const char *, ...);
typedef decltype(sizeof 0) size_t;
template <size_t N = 3> class V { public: double v[N]; V() { for (size_t i = 0; i < N; i++) v[i] = 0; } V(double a, double b, double c, double d) { v[0] = a; v[1] = b; v[2] = c; v[3] = d; } };
template <size_t I = 4> class Q : public V<I> { public: Q() {} Q(double a, double b, double c, double d) : V<I>(a, b, c, d) {} };
static bool near1(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }
template <size_t N> bool near1(const V<N> &a, const V<N> &b) { for (size_t i = 0; i < N; i++) if (!near1(a.v[i], b.v[i])) return false; return true; }
int main() {
    Q<> q1(1, 0, 0, 0);
    V<4> raw; raw.v[0] = 1;
    V<4> four(1, 0, 0, 0);
    printf("%d", near1(q1, raw));
    printf(" %d", near1(q1, q1));
    printf(" %d", near1(q1, Q<>(1, 0, 0, 0)));
    printf(" %d\n", near1(four, Q<>(1, 0, 0, 0)));
}
