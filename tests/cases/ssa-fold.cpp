// Folding, copy propagation and algebraic simplification on SSA (M3b.4 round 2): constants through promoted locals,
// comparisons, conversions, floating, the identities, block arguments of one value; what would trap stays on a path
// never taken. Its -O1 -dump=ssa=fold on x86_64-linux is ssa-fold.fold, which tests/ssa.sh holds.
#include <cstdio>

static int arith(int x) {
    int a = 3, b = 4;
    int c = a * b + 1;                  // 13
    unsigned u = 0u - 1u;               // a wrap, folded
    int s = (c << 2) >> 1;              // 26
    int d = x + 0;
    int e = d * 1 - (x - x);
    int z = 0;
    if (x == 12345) e = c / z + (-2147483647 - 1) / (z - 1) + (c << (z + 40));   // never run: three folds refused
    return c + s + e + static_cast<int>(u >> 28) + -(-x) + ~~x;
}

static int compare(int x) {
    int a = 7, b = -2;
    unsigned ua = 7u, ub = static_cast<unsigned>(b);
    return (a < b) + 2 * (ua < ub) + 4 * (x == x) + 8 * (x < x) + 16 * (a >= 7);
}

static double floating(double y) {
    double h = 1.5, t = 2.0;
    double p = h * t + 0.25;            // 3.25
    float q = static_cast<float>(p);
    double zero = 0.0;
    if (y > 1e300) p = zero / zero;     // never run: a division by zero is left to run
    return p + q + static_cast<double>(static_cast<int>(2.75)) + -(-y) + (h < t);
}

static int merge(int x) {
    int k = 5;
    if (x > 2) k = 5; else k = 5;       // two edges, one constant
    int m = x;
    if (x > 1) m = x;                   // two edges, one value
    int n = x;
    for (int i = 0; i < 3; i++) n = n;  // a loop that passes the value itself
    unsigned char w = static_cast<unsigned char>(k + 295);
    return k + m + n + w;
}

int main() {
    std::printf("%d %d %d\n", arith(4), arith(-9), arith(100));
    std::printf("%d %d\n", compare(1), compare(-1));
    std::printf("%.4f %.4f\n", floating(0.5), floating(-3.0));
    std::printf("%d %d\n", merge(1), merge(7));
    return 0;
}
