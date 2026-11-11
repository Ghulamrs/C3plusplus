// The SSA pass `rotate` (M3b.5): a loop left only by its head's test runs that test in its latch. Each function is one
// shape - a head that loads, values read past the loop, a call that may unwind, a loop never entered, and two ways out left alone.
#include <cstdio>

struct Shape { virtual int area(int k) const; virtual ~Shape(); };
struct Guard { int n; Guard(); ~Guard(); };
int Shape::area(int k) const { return k * 3 + 1; }
Shape::~Shape() {}
Guard::Guard() : n(0) {}
Guard::~Guard() { std::printf("guard %d\n", n); }

int length(const char *s) { int n = 0; while (s[n]) ++n; return n; }
unsigned fnv(const char *p) {
    unsigned x = 2166136261u;
    for (; *p; ++p) x = (x ^ (unsigned char)*p) * 16777619u;
    return x;
}
int sum(const int *a, int n) { int s = 0; for (int i = 0; i < n; ++i) s += a[i]; return s; }
int past(int n) { int i = 0, s = 0; while (i * i < n) { s += i; ++i; } return s * 100 + i; }
int find(const int *a, int n, int x) { int i = 0; while (i < n && a[i] != x) ++i; return i; }
int calls(const Shape &v, int n) { int s = 0; for (int i = 0; i < n; ++i) s += v.area(i); return s; }
int guarded(const Shape &v, int n) { Guard g; for (int i = 0; i < n; ++i) g.n += v.area(i & 1); return g.n; }
int nested(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i; j < n; ++j) if ((i ^ j) & 1) s += j; else s -= i;
    return s;
}
int skips(int n) { int s = 7; for (int i = n; i < 0; ++i) s += i; return s; }

int main() {
    int a[9] = { 4, 8, 15, 16, 23, 42, 7, 1, 9 };
    Shape v;
    std::printf("%d %d %u %u\n", length(""), length("rotation"), fnv(""), fnv("the head's test"));
    std::printf("%d %d %d %d\n", sum(a, 0), sum(a, 9), past(0), past(50));
    std::printf("%d %d %d\n", find(a, 9, 42), find(a, 9, 5), find(a, 0, 4));
    std::printf("%d %d\n", calls(v, 0), calls(v, 10));
    const int none = guarded(v, 0);
    const int five = guarded(v, 5);
    std::printf("%d %d %d %d %d\n", none, five, nested(0), nested(7), skips(3) + skips(-4));
    return 0;
}
