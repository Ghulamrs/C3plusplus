// A class template over a non-type parameter made from the read under -templates=subst: another template's id over an
// expression of the values (`V<N + 1>`, `V<(M * N) % 5 + 1>`), folded and keyed by its value, in a data member and a signature.
extern "C" int printf(const char *, ...);

template <int N> struct V { int s[N]; };

template <int N> struct Grow {
    V<N + 1> more;
    V<N * 2 - 1> odd;
    int last() const { return more.s[N]; }
    const V<N + 1> &get() const { return more; }
};

template <class T, int M, int N> struct Pair {
    T tag;
    V<(M * N) % 5 + 1> cells;
    V<M / 2> half;
};

int main() {
    Grow<3> g;
    g.more.s[3] = 7;
    g.odd.s[4] = 2;
    printf("%d %d %d %d\n", g.last(), g.get().s[3] + g.odd.s[4], static_cast<int>(sizeof(Grow<3>)), static_cast<int>(sizeof(g.odd)));
    Pair<char, 3, 4> p;
    p.tag = 'q';
    p.cells.s[2] = 5;
    p.half.s[0] = 6;
    printf("%c %d %d %d\n", p.tag, p.cells.s[2] + p.half.s[0], static_cast<int>(sizeof(p.cells)), static_cast<int>(sizeof(Pair<char, 3, 4>)));
    V<4> w;
    w.s[0] = 1;
    Grow<1> h;
    h.more.s[1] = 9;
    printf("%d %d %d\n", w.s[0], h.last(), static_cast<int>(sizeof(Grow<1>)));
    return 0;
}
