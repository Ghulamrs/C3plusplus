// A class template over a non-type parameter made from the read under -templates=subst: a data member's bound folded under
// the values (`T slot[N + 1]`, `char cells[R * C + (R - 1)]`, `int w[(N + 1) / 2], h`), its size read in a member body.
extern "C" int printf(const char *, ...);

template <class T, int N> struct Ring {
    T slot[N + 1];
    int head;
    int capacity() const { return N; }
    int bytes() const { return static_cast<int>(sizeof slot); }
    void fill(T v) { for (int i = 0; i <= N; i++) slot[i] = v + i; head = N; }
};

template <int R, int C> struct Grid { char cells[R * C + (R - 1)]; short tag; };

template <unsigned N> struct Half { int w[(N + 1) / 2], h; };

int main() {
    Ring<int, 3> a;
    a.fill(10);
    Ring<double, 1> b;
    b.fill(0.5);
    printf("%d %d %d %d\n", a.capacity(), a.bytes(), a.slot[3], static_cast<int>(sizeof(Ring<int, 3>)));
    printf("%d %d %.1f %d\n", b.capacity(), b.bytes(), b.slot[1], static_cast<int>(sizeof(Ring<double, 1>)));
    Grid<2, 3> g;
    Grid<1, 1> one;
    g.tag = 5, one.tag = 6;
    printf("%d %d %d %d %d\n", static_cast<int>(sizeof g.cells), static_cast<int>(sizeof g), static_cast<int>(sizeof one.cells),
           static_cast<int>(sizeof one), g.tag + one.tag);
    Half<5> h5;
    Half<1> h1;
    h5.h = 7, h1.h = 8;
    printf("%d %d %d %d %d\n", static_cast<int>(sizeof h5.w / sizeof h5.w[0]), static_cast<int>(sizeof h5),
           static_cast<int>(sizeof h1.w / sizeof h1.w[0]), static_cast<int>(sizeof h1), h5.h + h1.h);
    return 0;
}
