// A class template specialization first named inside another template's pattern - here the
// out-of-class conversion of V<N>, whose own parameter is also `N` - reads its own `N` as the
// value it was given. cpp11 read it as the pattern's: Mx<3, 3>'s rows became V<N> unresolved and
// `R[0][0]` was refused (found compiling a CMatrix/CVector pair that clang takes).
extern "C" int printf(const char *, ...);

template <int M, int N> struct Mx;
template <int N> struct V {
    double s[N];
    double operator[](int i) const { return s[i]; }
    double &operator[](int i) { return s[i]; }
    explicit operator Mx<3, 3>() const;
};
template <int M, int N> struct Mx {
    V<N> v[M];
    const V<N> &operator[](int i) const { return v[i]; }
    V<N> &operator[](int i) { return v[i]; }
};
template <int N> V<N>::operator Mx<3, 3>() const {
    Mx<3, 3> m;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i][j] = i == j ? s[i] : 0;
    return m;
}
double trace(const Mx<3, 3> &R) { return R[0][0] + R[1][1] + R[2][2]; }

int main() {
    V<3> d;
    d[0] = 1; d[1] = 2; d[2] = 3.5;
    printf("%g %d\n", trace(static_cast<Mx<3, 3> >(d)), (int)sizeof(V<3>));
    return 0;
}
