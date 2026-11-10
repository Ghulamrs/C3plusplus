// mem2reg (M3b.4 round 1): scalars promoted across a merge, a loop, a switch and an invoke's normal edge; an address
// taken, an array, a class and a slot live into a handler stay in the frame. Its -O1 -dump=ssa=promote on x86_64-linux
// is ssa-promote.promote, which tests/ssa.sh holds.
#include <cstdio>

struct Pair { int a, b; };

static int pick(int x) {
    int r;
    if (x > 3) r = x * 2; else r = x - 1;
    return r;
}

static double sum(int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += i * 0.5;
    return s;
}

static int classify(int x) {
    int k = 0;
    switch (x) {
    case 1: k = 10; break;
    case 2: k = 20; break;
    case 3: k = 30;
    default: k += 1; break;
    }
    return k;
}

static void bump(int *p) { ++*p; }

static int escaped(int x) {
    int y = x;
    bump(&y);
    return y;
}

static int mayThrow(int x) {
    if (x < 0) throw x;
    return x + 1;
}

static int guarded(int x) {
    int before = x * 3;
    int after = 0;
    try {
        after = mayThrow(x);
        after = after + mayThrow(after);
    } catch (int e) {
        after = before + e;
    }
    return after;
}

static int arrays(int n) {
    int a[4] = {1, 2, 3, 4};
    Pair p = {n, n + 1};
    int t = 0;
    for (int i = 0; i < 4; i++) t += a[i];
    return t + p.a + p.b;
}

static const char *name(bool b) {
    const char *s = b ? "yes" : "no";
    return s;
}

int main() {
    std::printf("%d %d\n", pick(2), pick(5));
    std::printf("%.1f\n", sum(5));
    std::printf("%d %d %d %d\n", classify(1), classify(2), classify(3), classify(9));
    std::printf("%d\n", escaped(41));
    std::printf("%d %d\n", guarded(4), guarded(-2));
    std::printf("%d\n", arrays(5));
    std::printf("%s %s\n", name(true), name(false));
    return 0;
}
