// Value numbering on SSA (M3b.4 round 4): an expression computed again where its first dominates, commuted operands,
// comparisons, conversions and an array's element address found again; loads of one address within a block, after a
// store, and not across a call or a store elsewhere; a const local promoted; a handler's twin of the body's value.
// Its -O1 -dump=ssa=gvn on x86_64-linux is ssa-gvn.gvn, which tests/ssa.sh holds.
#include <cstdio>

int g = 3;
int h = 4;

struct Pair { int a, b; };

static int bump() { g += 1; return g; }

static int mayThrow(int x) {
    if (x < 0) throw x;
    return x;
}

static int pure(int a, int b, int c) {
    int r = a * b + c;
    if (a > b) r += b * a + c;                      // commuted, under the first: the same value
    if (a > b) r += a > b;                          // the comparison again
    double d = a;
    double e = a;                                   // the conversion again
    return r + static_cast<int>(d * e);
}

static int memory(int x) {
    int s = g + g;                                  // one load
    g = x;
    s += g;                                         // the value just stored
    s += bump();
    s += g;                                         // a call between: loaded again
    h = 9;
    s += g;                                         // a store elsewhere between: loaded again
    return s;
}

static int elements(int i, Pair *p) {
    int arr[4] = { 1, 2, 3, 4 };
    int t = arr[i] + arr[i];
    return t + p->a * p->a + p->b;
}

static int constant(int x) {
    const int k = x * 2;                            // a const scalar: promoted
    int r = k;
    for (int i = 0; i < 3; i++) r += k + i;
    return r;
}

static int handler(int x) {
    int before = x * 7, caught = 0;
    try {
        before += mayThrow(x);
    } catch (int e) {
        caught = e + x * 7;                         // a funclet on Windows: its own x * 7
    }
    return caught != 0 ? caught : before + x * 7;
}

int main() {
    std::printf("%d %d\n", pure(5, 2, 1), pure(1, 4, 3));
    const int m = memory(10);
    std::printf("%d %d %d\n", m, g, h);
    Pair p = { 6, 7 };
    std::printf("%d %d\n", elements(2, &p), elements(0, &p));
    std::printf("%d %d\n", constant(3), constant(-1));
    std::printf("%d %d\n", handler(2), handler(-3));
    return 0;
}
