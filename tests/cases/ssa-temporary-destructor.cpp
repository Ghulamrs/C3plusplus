// A class temporary with a destructor, built by the constructor the parser chose and destroyed at the end of its full
// expression - an expression statement, a scalar's initialiser, a condition, a for's parts, a return's value - last built
// first, and by its cleanup where a call made while it is alive throws; and a call's result of such a class, the caller's
// to destroy the same way (M3b.2, the SSA lowering's temporaries).
extern "C" int printf(const char *, ...);

static int built = 0, gone = 0;

struct K {
    int v;
    K(int x);
    K(int x, int y);
    ~K();
    int get() const;
};

K::K(int x) : v(x) { built++; printf("+%d ", v); }
K::K(int x, int y) : v(x * 10 + y) { built++; printf("+%d ", v); }
K::~K() { gone++; printf("-%d ", v); }
int K::get() const { return v; }

static int take(const K &k) { printf("take%d ", k.v); return k.v; }
static int both(const K &a, const K &b) { return a.v + b.v; }
static int boom(const K &k) { if (k.v > 50) throw k.v; return k.v; }

static K make(int n) { return K(n); }

static int results() {
    take(make(11));
    int a = make(12).get();
    make(13);
    int b = take(K(make(14).get() + 1));
    printf("| %d %d\n", a, b);
    return a + b;
}

static int statements() {
    take(K(1));
    int a = take(K(2, 3));
    int b = take(K(take(K(4))));
    int c = K(5).get();
    K(6).get();
    int d = both(K(7), K(7));       // one value twice: the order arguments are built in is unspecified
    printf("| %d %d %d %d\n", a, b, c, d);
    return a + b + c + d;
}

static int conditions(int n) {
    int r = 0;
    if (take(K(n)) > 2) r += 1;
    int i = 0;
    while (take(K(i)) < n) i++;
    printf("| %d %d\n", r, i);
    return r + i;
}

static int loops(int n) {
    int r = 0;
    for (int i = take(K(0)); i < take(K(n)); i += take(K(1))) r++;
    int j = 0;
    do j++; while (take(K(j)) < 2);
    return r * 10 + j;
}

static int returns(int n) {
    K local(n * 100);
    return take(K(n)) + 1;
}

static void returnsVoid(int n) {
    return (void)take(K(n));
}

static int unwinds(int n) {
    K local(90);
    int r = 0;
    try {
        r = boom(K(n));
        take(K(n + 1));
    } catch (int e) {
        r = -e;
    }
    printf("| %d\n", r);
    return r;
}

static int shortCircuit(int n) {
    int r = n > 0 && take(K(n)) > 1;
    return r + (n < 0 ? take(K(-n)) : 0);
}

int main() {
    int s = statements();
    int c = conditions(3);
    int m = results();
    int l = loops(2);
    int r = returns(4);
    returnsVoid(5);
    printf("| %d %d %d\n", m, l, r);
    int u = unwinds(60);
    int v = unwinds(9);
    int w = shortCircuit(2);
    w += shortCircuit(0);
    w += shortCircuit(-3);
    printf("%d %d %d %d %d\n", s, c, u, v, w);
    printf("built %d gone %d\n", built, gone);
    return 0;
}
