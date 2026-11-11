// LICM and counter widening on SSA (M3b.5 round 3): an invariant product hoisted to the preheader and a division the
// loop may never reach left in it, a loop entered by a guard given a preheader and its division hoisted, a nested
// loop's invariant hoisted twice, an int index carried at 64 bits. Its -O2 -dump=ssa=loops is ssa-loops.loops.
#include <cstdio>

static int sum(const int *a, int n, int x, int y) {
    int s = 0;
    for (int i = 0; i < n; i++) s += a[i] * (x * y) + x / y;
    return s;
}

static int guarded(const int *a, int n, int x) {
    int s = 0, i = 0;
    if (n > 0) do { s += a[i] + (x << 2) + x / n; i++; } while (i < n);
    return s;
}

static long long nested(const int *a, int n, int m, int k) {
    long long t = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) t += a[j] + (k * 7 + 1) * i;
    return t;
}

static int skipped(const int *a, int n, int d) {
    int s = 0;
    for (int i = 0; i < n; i++) if (a[i] > 100) s += a[i] / d;    // d is 0: never divided, no a[i] passes 100
    return s;
}

int main() {
    int a[16];
    for (int i = 0; i < 16; i++) a[i] = i * 3 - 5;
    const int zero = a[0] + 5;
    std::printf("%d %d\n", sum(a, 16, 3, 2), sum(a, 0, 1, zero));
    std::printf("%d %d\n", guarded(a, 16, 5), guarded(a, 0, 1));
    std::printf("%lld\n", nested(a, 5, 16, 2));
    std::printf("%d\n", skipped(a, 16, zero));
    return 0;
}
