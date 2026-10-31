// `new T[n]` of a class with constructors and no destructor: each element built by the default constructor, in order,
// n read once; and [expr.new]/20 where one of them throws, in a function with landing pads - the storage given back,
// the exception caught by the caller. Lowered under -front=ssa (M3b.2).
extern "C" int printf(const char *, ...);

static int made, failAt = -1;
struct Cell {
    int order, value;
    Cell();
};
Cell::Cell() : order(made++), value(7) {
    if (order == failAt) throw order;
}

static int counted;
static int howMany(int k) { counted++; return k; }

static Cell *row(int k) { return new Cell[howMany(k)]; }

static int guarded(int k) {
    Cell keep;
    try {
        Cell *c = new Cell[k];
        int s = c[k - 1].order - c[0].order;
        delete[] c;
        return s;
    } catch (int e) {
        return 1000 + e - keep.order;
    }
}

int main() {
    Cell *a = row(4);
    printf("%d %d %d %d\n", a[0].order, a[3].order, a[2].value, counted);
    delete[] a;
    Cell *none = row(0);
    printf("%d %d\n", made, counted);
    delete[] none;
    printf("%d\n", guarded(3));
    failAt = made + 3;
    const int g = guarded(5);
    printf("%d %d\n", g, made);
    return 0;
}
