// Loads hoisted and counters widened on SSA (M3b.5, the loads lane): a bound read through a pointer in a loop that
// stores nothing, a global read while a local array is written, one read and written that stays, one beside a call stays;
// a counter from a run-time start counting down, one tested with `!=`, an index plus one. Its -O2 -dump=ssa=loops is
// ssa-loads.loops.
#include <cstdio>

struct Box { int n; int v[16]; };
static int scale = 3;
static int table[16];

static int bounded(const Box *b) {
    int s = 0;
    for (int i = 0; i < b->n; i++) s += b->v[i];
    return s;
}

static int scaled(int n) {
    int out[16];
    for (int i = 0; i < n; i++) out[i] = i * scale;
    int s = 0;
    for (int i = 0; i < n; i++) s += out[i];
    return s;
}

static int written(int n) {
    for (int i = 0; i < n; i++) { table[i] = scale; scale = scale + 1; }
    return table[n - 1] + scale;
}

static int called(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += scale + std::printf("%s", "");
    return s;
}

static int shift(int *a, int from) {
    int j = from;
    while (j > 0 && a[j - 1] > a[j]) { int t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; --j; }
    return j;
}

static long long until(const int *a, int from, int to) {
    long long s = 0;
    for (int i = from; i != to; i += 2) s += a[i] + a[i + 1];
    return s;
}

int bump(int x) { return x + scale; }

int main() {
    Box b;
    b.n = 12;
    for (int i = 0; i < 16; i++) b.v[i] = 20 - i * 3;
    std::printf("%d %d\n", bounded(&b), scaled(10));
    const int w = written(5);
    std::printf("%d %d\n", w, called(4));
    const int at = shift(b.v, 12);
    std::printf("%d %d\n", at, b.v[0]);
    std::printf("%lld %lld\n", until(b.v, 2, 14), until(b.v, 4, 4));
    return 0;
}
