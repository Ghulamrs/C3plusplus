// Selection under the passes: a frame address made at each use (and one a callee reads below), a constant shift count,
// a loop around a call that may throw, a loop that tests at its foot, a multiply-high beside values kept in registers.
#include <stdio.h>

struct Guard { int n; Guard(int x); ~Guard(); };
Guard::Guard(int x) : n(x) {}
Guard::~Guard() { printf("~Guard %d\n", n); }

struct Shape { virtual ~Shape(); virtual int area() const = 0; };
Shape::~Shape() {}
struct Sq : Shape { int s; Sq(int x); int area() const; };
Sq::Sq(int x) : s(x) {}
int Sq::area() const { return s * s; }
struct Rc : Shape { int w, h; Rc(int a, int b); int area() const; };
Rc::Rc(int a, int b) : w(a), h(b) {}
int Rc::area() const { if (w < 0) throw w; return w * h; }

int before(const int *p) { return p[-1] * 100 + p[0]; }

int frame(int k) {
    int a[4];
    a[0] = k; a[1] = k + 1; a[2] = k + 2; a[3] = k + 3;
    int *end = a + 4;
    int s = 0;
    for (int *p = a; p != end; ++p) s += *p;
    return s + before(&a[1]) + before(a + 3);
}

unsigned hashes(int rounds) {
    char buf[32];
    unsigned h = 0;
    for (int r = 0; r < rounds; ++r) {
        for (int i = 0; i < 31; ++i) buf[i] = (char)('a' + (r + i) % 26);
        buf[31] = 0;
        unsigned x = 2166136261u;
        for (const char *p = buf; *p; ++p) x = (x ^ (unsigned char)*p) * 16777619u;
        h ^= x >> 3;
        h += x << 5;
    }
    return h;
}

long long virt(int n, int bad) {
    Guard g(n);
    Sq a(3);
    Rc b(bad, 5);
    Shape *p[2] = {&a, &b};
    long long s = 0;
    for (int i = 0; i < n; ++i) s += p[i & 1]->area();
    return s;
}

int foot(int n) {
    int s = 0, i = 0;
    do { s += i * 3; ++i; } while (i < n);
    return s;
}

int main() {
    printf("%d %d\n", frame(1), frame(10));
    printf("%u %u\n", hashes(1), hashes(50));
    printf("%lld\n", virt(7, 4));
    try { virt(5, -2); } catch (int e) { printf("caught %d\n", e); }
    printf("%d %d\n", foot(0), foot(9));
    return 0;
}
