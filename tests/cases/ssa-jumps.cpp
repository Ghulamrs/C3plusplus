// Jump threading on SSA (M3b.4 round 5): a flag set before a loop's exit and tested after it, a short-circuit && and
// || tested again, a state machine's switch on a value each arm sets, a loop entered with its test already known, and
// a value read at a join below the test, which refuses the edge. Its -O1 -dump=ssa=jumps on x86_64-linux is ssa-jumps.jumps.
#include <cstdio>

static int find(const int *a, int n, int x) {
    bool found = false;
    int i = 0;
    for (; i < n; i++)
        if (a[i] == x) { found = true; break; }
    if (found) return i;
    return -1;
}

static int both(int a, int b) {
    if (a > 0 && b > 0) return a + b;
    if (a < 0 || b < 0) return a - b;
    return 0;
}

static int machine(const char *s) {
    int state = 0, words = 0;
    for (; *s; s++) {
        switch (state) {
        case 0: if (*s != ' ') { state = 1; words++; } break;
        case 1: if (*s == ' ') state = 0; break;
        }
    }
    return words * 10 + state;
}

static int countdown(int n) {
    bool more = true;
    int steps = 0;
    while (more) {
        steps++;
        n -= 3;
        more = n > 0;
    }
    return steps;
}

static void report(int v) { std::printf("report %d\n", v); }

static int joined(int c) {
    int v = c > 5 ? 1 : 2;
    if (v == 1) report(c);
    report(v);
    return v;
}

int main() {
    const int a[] = { 4, 8, 15, 16, 23, 42 };
    std::printf("%d %d %d\n", find(a, 6, 15), find(a, 6, 7), find(a, 0, 4));
    std::printf("%d %d %d %d\n", both(2, 3), both(-2, 3), both(2, -3), both(0, 0));
    std::printf("%d %d %d\n", machine("the quick  brown fox"), machine(""), machine("  one "));
    std::printf("%d %d %d\n", countdown(10), countdown(1), countdown(-4));
    const int j9 = joined(9);
    const int j1 = joined(1);
    std::printf("%d %d\n", j9, j1);
    return 0;
}
