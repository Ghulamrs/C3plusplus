// The SSA pass `regvn` (M3b.5 lane mm): after `rotate`, an inner loop's guard repeats a test a dominating guard already
// took, and becomes a jump. One shape a function: three loops under one bound, two bounds, a repeated test, two loops.
#include <cstdio>

static int grid[6][6];
int fill(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < n; ++j) { grid[i][j] += k + 1; s += grid[i][j]; }
    return s;
}
int two(int n, int m) {
    int s = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) s += i * j + 1;
    return s;
}
int checked(int n) {
    int s = 3;
    if (0 < n)
        for (int j = 0; j < n; ++j) s += j;
    return s;
}
int after(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i) s += i;
    for (int j = 0; j < n; ++j) s += 2 * j;
    return s;
}

int main() {
    const int none = fill(0);
    const int one = fill(1);
    const int six = fill(6);
    std::printf("%d %d %d\n", none, one, six);
    std::printf("%d %d %d %d\n", two(0, 3), two(3, 0), two(4, 5), two(-1, -1));
    std::printf("%d %d %d %d %d\n", after(0), after(-3), after(9), checked(0), checked(5));
    return 0;
}
