// A function template's declaration is read with the deduced arguments bound, and the calling
// function's locals are not in its scope: `Mx<4,4> M; M * v` read the operator's `V<M>` as the
// local `M`, the substitution failed, and no operator was a candidate. Found compiling the COM
// Core sources, where `CMatrix<4, 4> M = ...; M * x` is the projection.
extern "C" int printf(const char *, ...);

template <int N> struct V { double s[N]; V() {} explicit V(double f) { for (int i = 0; i < N; i++) s[i] = f + i; } };
template <int M, int N> struct Mx { V<N> r[M]; explicit Mx(double f) { for (int i = 0; i < M; i++) r[i] = V<N>(f * (i + 1)); } };
template <int M, int N> V<M> operator*(const Mx<M, N> &a, const V<N> &x) {
    V<M> out;
    for (int i = 0; i < M; i++) { out.s[i] = 0; for (int j = 0; j < N; j++) out.s[i] += a.r[i].s[j] * x.s[j]; }
    return out;
}
template <int K> int scaled(int v) { return v * K; }

int main() {
    Mx<2, 3> M(1.0);
    V<3> N(0.5);
    int K = 7;
    V<2> r = M * N;
    printf("%g %g %d\n", r.s[0], r.s[1], scaled<3>(K) + K);
    return 0;
}
