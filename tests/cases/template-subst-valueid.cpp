// A class template over a non-type parameter made from the read under -templates=subst: another template's id over the value
// (`V<N> r[M]`, `const V<N> &row`), keyed and made first by the value, in a data member and a signature.
extern "C" int printf(const char *, ...);

template <int N> struct V { int s[N]; };

template <int M, int N> struct Mx {
    V<N> r[M];
    const V<N> &row(int i) const { return r[i]; }
    int cells() const { return M * N; }
};

template <class T, int N> struct Tagged {
    V<N> a;
    T tag;
    V<N> first() const { return a; }
};

int main() {
    Mx<2, 3> m;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++) m.r[i].s[j] = i * 10 + j;
    Tagged<char, 4> p;
    p.a.s[3] = 9;
    p.tag = 'x';
    printf("%d %d %d\n", m.row(1).s[2], m.cells(), static_cast<int>(sizeof(Mx<2, 3>)));
    printf("%d %c %d\n", p.first().s[3], p.tag, static_cast<int>(sizeof(Tagged<char, 4>)));
    return 0;
}
