// Constructors the SSA lowering takes past a simple class: a class template's, named by the template; a member of a
// class with constructors and no copy of its own, copied as bytes from a mem-initialiser; a member that is an array
// of arrays of scalars, zeroed by `: R()` or left alone; and members of an anonymous union, one named per list.
extern "C" int printf(const char *, ...);

struct It {
    int *p;
    int step;
    It(int *q, int s) : p(q), step(s) {}
    int at(int i) const { return p[i * step]; }
};

struct Found {
    It where;
    bool hit;
    Found(const It &w, const bool &h) : where(w), hit(h) {}
};

template <class T>
struct Box {
    T value;
    int tag;
    Box(T v, int t) : value(v), tag(t) {}
    Box() : value(), tag(-1) {}
    T twice() const { return value + value; }
};

struct Grid {
    double R[2][3];
    int n;
    Grid() : R(), n(0) {}
    Grid(double d) : R(), n(1) { R[1][2] = d; }
    double sum() const {
        double s = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 3; j++) s += R[i][j];
        return s;
    }
};

struct Val {
    int kind;
    union {
        int i;
        double d;
    };
    Val(int k) : kind(1), i(k * 10) {}
    Val(double x) : kind(2), d(x) {}
    ~Val() { printf("~Val %d\n", kind); }
};

static Found search(int *a, int n, int want) {
    It it(a, 1);
    for (int i = 0; i < n; i++)
        if (it.at(i) == want) return Found(It(a + i, 1), true);
    return Found(it, false);
}

int main() {
    int a[5] = { 3, 5, 8, 13, 21 };
    Found f = search(a, 5, 13);
    Found g = search(a, 5, 4);
    printf("%d %d %d %d\n", f.hit, f.where.at(0), g.hit, g.where.at(2));
    Box<int> bi(21, 1);
    Box<double> bd(1.25, 2);
    Box<int> be;
    printf("%d %d %g %d %d %d\n", bi.twice(), bi.tag, bd.twice(), bd.tag, be.value, be.tag);
    Grid z;
    Grid d(2.5);
    printf("%g %d %g %d %g\n", z.sum(), z.n, d.sum(), d.n, d.R[1][2]);
    {
        Val vi(4);
        Val vd(0.5);
        printf("%d %d %d %g\n", vi.kind, vi.i, vd.kind, vd.d);
    }
    return 0;
}
