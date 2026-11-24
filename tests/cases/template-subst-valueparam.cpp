// A class template over a non-type parameter made from the read under -templates=subst: the value named in member bodies
// alone (an int, a bool, an enumeration), which are replayed under it; and an array bound `[N]`, laid under the value.
extern "C" int printf(const char *, ...);

enum Colour { Red = 3, Blue = 7 };

template <class T, int N> struct Scaled {
    T v;
    T get() const { return v * N; }
    int scale() const { return N + 1; }
};

template <bool B> struct Flag {
    int x;
    int pick(int a, int b) const { if (B) return a; return b; }
};

template <Colour C, class T> struct Tinted {
    T a;
    T b;
    int shade() const { return static_cast<int>(C) * 10 + static_cast<int>(a + b); }
};

template <class T, int N> struct Buffer {
    T items[N];
    int size() const { return N; }
};

template <class T, int R, int C> struct Matrix {
    T m[R][C];
    int cells() const { return R * C; }
};

int main() {
    Scaled<int, 3> s;
    s.v = 5;
    Scaled<char, 2> c;
    c.v = 4;
    printf("%d %d %d %d\n", s.get(), s.scale(), static_cast<int>(c.get()), c.scale());
    Flag<true> t;
    Flag<false> f;
    t.x = f.x = 0;
    printf("%d %d %d\n", t.pick(1, 2), f.pick(1, 2), static_cast<int>(sizeof(Flag<true>)));
    Tinted<Red, int> r;
    r.a = 1;
    r.b = 2;
    Tinted<Blue, short> b;
    b.a = 4;
    b.b = 5;
    printf("%d %d %d\n", r.shade(), b.shade(), static_cast<int>(sizeof(Tinted<Blue, short>)));
    Buffer<int, 4> buf;
    buf.items[3] = 9;
    printf("%d %d %d\n", buf.size(), buf.items[3], static_cast<int>(sizeof buf));
    Matrix<short, 2, 3> mx;
    mx.m[1][2] = 5;
    printf("%d %d %d\n", mx.cells(), mx.m[1][2], static_cast<int>(sizeof mx));
    return 0;
}
