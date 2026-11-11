// Unrolling on SSA (M3b.5 round 4): loops counted by constants copied whole - an array filled and summed, a counter
// read after its loop, an unsigned count down to zero - and 4 times where too long, the exit's test in the copy the
// count leaves by; a loop with a call and one counted to a variable kept. Its -O2 -dump=ssa=unroll is ssa-unroll.unroll.
#include <cstdio>

static int filled() {
    int a[8];
    for (int i = 0; i < 8; i++) a[i] = i * 3 - 5;
    int s = 0;
    for (int i = 0; i < 8; i++) s += a[i] * a[i];
    return s;
}

static int after() {
    int t = 0, i;
    for (i = 0; i < 37; i += 3) t += i;
    return t * 100 + i;
}

static unsigned down(unsigned x) {
    for (unsigned u = 10; u != 0; u--) x = x * 3 + u;
    return x;
}

static unsigned long long longer(const int *a) {
    unsigned long long h = 7;
    for (int i = 0; i < 102; i++) h = h * 31 + static_cast<unsigned>(a[i & 7] + i);
    return h;
}

static int rotated(int x) {
    int i = 0;
    do { x = x * 5 % 1009 + i; i += 2; } while (i < 250);
    return x;
}

static int nested(int n) {
    int s = 0;
    for (int j = 0; j < n; j++)
        for (int k = 0; k < 4; k++) s += j * k + 1;
    return s;
}

static int called() {
    int s = 0;
    for (int i = 0; i < 3; i++) s += std::printf("%d ", i);
    std::printf("\n");
    return s;
}

static int unknown(int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = v * 7 + i;
    return v;
}

int main() {
    int a[8];
    for (int i = 0; i < 8; i++) a[i] = i * i - 9;
    std::printf("%d %d %u\n", filled(), after(), down(1));
    std::printf("%llu %d %d\n", longer(a), rotated(3), nested(6));
    std::printf("%d %d\n", called(), unknown(a[3] + 5));
    return 0;
}
