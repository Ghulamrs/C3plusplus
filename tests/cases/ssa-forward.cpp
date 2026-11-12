// The SSA pass `gvn` (M3b.5 lane hash): a load found in the one block before it, reached by br or condbr, is that block's
// value - a loop's test and its body read one byte once - and not past a store, a call, a join or a volatile access.
// Its -O1 -dump=ssa=gvn on x86_64-linux is ssa-forward.gvn, which tests/ssa.sh holds.
#include <cstdio>

int g = 5;
volatile int v = 7;

static unsigned hash(const char *s) {
    unsigned x = 2166136261u;
    for (const char *p = s; *p; ++p) x = (x ^ (unsigned char)*p) * 16777619u;   // the test's load, the body's
    return x;
}

static int under(const int *p) {
    if (*p > 2) return *p * 10;                     // the then-block's load is the test's
    return *p;                                      // and so is the else-block's
}

static int stored(int *p, int *q) {
    if (*p > 0) { *q = 9; return *p; }              // a store between: loaded again
    return 0;
}

static int bump() { g += 1; return g; }

static int called(const int *p) {
    int a = *p;
    bump();
    if (a > 0) return *p + a;                       // a call between: loaded again
    return a;
}

static int joined(const int *p, int c) {
    int a = 0;
    if (c) a = *p;
    else a = -*p;
    return a + *p;                                  // a join: two ways in, loaded again
}

static int quiet() {
    int a = v;
    if (a > 0) return v + a;                        // volatile: loaded again
    return a;
}

int main() {
    char word[] = "forward";
    int one = 3, two = 1, three = 4;
    std::printf("%u\n", hash(word));
    std::printf("%u\n", hash(""));
    std::printf("%d %d\n", under(&one), under(&two));
    const int s1 = stored(&one, &three);
    std::printf("%d %d\n", s1, three);
    std::printf("%d\n", stored(&three, &three));
    const int c1 = called(&g);
    std::printf("%d %d\n", c1, g);
    std::printf("%d %d\n", joined(&one, 1), joined(&one, 0));
    std::printf("%d\n", quiet());
    return 0;
}
