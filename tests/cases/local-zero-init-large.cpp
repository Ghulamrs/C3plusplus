// A local aggregate's list that leaves most of it zero: `= {0}`, `= {}`, a few values then a long zero run, nested
// arrays, a class of arrays, and a member with a constructor among them - each zeroed run is one memset, not a store
// per element. The stack is dirtied first so a missed zero shows.
#include <cstdio>

struct Big { int a[2048]; char b[8192]; double d[16]; int *p; };
struct Counted { int v; Counted(); };
Counted::Counted() : v(7) {}
struct Mixed { int head[40]; Counted c; int tail[40]; };

int sink;

static void dirty() {
    char junk[40000];
    for (int i = 0; i < 40000; i++) junk[i] = static_cast<char>(i * 7 + 1);
    for (int i = 0; i < 40000; i += 97) sink += junk[i];
}

static long sumInts(const int *p, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) s += p[i] * (i % 13 + 1);
    return s;
}

static void ints() {
    int z[4096] = {0};
    int e[4096] = {};
    int few[3000] = {5, 6, 7};
    int small[10] = {1, 2};
    std::printf("z %ld e %ld few %ld small %ld\n", sumInts(z, 4096), sumInts(e, 4096), sumInts(few, 3000), sumInts(small, 10));
}

static void nested() {
    int grid[64][64] = {{1}, {2, 3}};
    short rows[100][30] = {{1, 2, 3}};
    long s = 0;
    for (int i = 0; i < 64; i++) s += sumInts(grid[i], 64) * (i + 1);
    int t = 0;
    for (int i = 0; i < 100; i++) for (int j = 0; j < 30; j++) t += rows[i][j] * (j + 1);
    std::printf("grid %ld rows %d\n", s, t);
}

static void classes() {
    Big b = {0};
    Big c = {};
    Big d = {{9}, {'x'}, {2.5}};
    long s = sumInts(b.a, 2048) + sumInts(c.a, 2048);
    int chars = 0;
    for (int i = 0; i < 8192; i++) chars += b.b[i] + c.b[i] + d.b[i];
    double ds = 0;
    for (int i = 0; i < 16; i++) ds += b.d[i] + c.d[i] + d.d[i];
    std::printf("big %ld %ld chars %d doubles %.1f null %d %d %d\n", s, sumInts(d.a, 2048), chars, ds,
                b.p == 0, c.p == 0, d.p == 0);
}

static void mixed() {
    Mixed m = {{4}};
    std::printf("mixed %ld %d %ld\n", sumInts(m.head, 40), m.c.v, sumInts(m.tail, 40));
}

int main() {
    dirty();
    ints();
    dirty();
    nested();
    dirty();
    classes();
    dirty();
    mixed();
    return 0;
}
