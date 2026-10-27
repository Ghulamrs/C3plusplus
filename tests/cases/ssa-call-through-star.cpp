// A class object called through `*p` - its operator() on the object p points at, [over.call.object] - from a pointer
// parameter, a local pointer, `*this` and a pointer to a const object (M3b.2, the SSA lowering's member calls).
extern "C" int printf(const char *, ...);

struct Add {
    int base;
    int operator()(int x) const { return base + x; }
    int operator()(int x, int y) { base += x * y; return base; }
    int twice(int x) { return (*this)(x, 2); }
};

static int viaPointer(Add *a, int x) { return (*a)(x); }
static int viaConst(const Add *a) { return (*a)(100); }
static int mutate(Add *a) { return (*a)(3, 4); }

int main() {
    Add a;
    a.base = 10;
    Add *p = &a;
    printf("%d\n", viaPointer(p, 5));
    printf("%d\n", (*p)(1));
    printf("%d\n", mutate(p));
    printf("%d\n", viaConst(&a));
    printf("%d\n", a.twice(5));
    printf("%d\n", a.base);
    return 0;
}
