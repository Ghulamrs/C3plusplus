// A 4-byte value read only at 32 bits keeps no extension: each chain below ends in a reader that needs all 64 bits
// (an index, a long long, an argument), so a missing extension where one is owed prints a wrong number.
#include <cstdio>

struct Shape { virtual ~Shape(); virtual int area() const = 0; };
Shape::~Shape() {}
struct Sq : Shape { int s; Sq(int x); int area() const; };
Sq::Sq(int x) : s(x) {}
int Sq::area() const { return s * s; }
struct Rc : Shape { int w, h; Rc(int a, int b); int area() const; };
Rc::Rc(int a, int b) : w(a), h(b) {}
int Rc::area() const { return w * h; }

long long virt(int n) {
    Sq a(3);
    Rc b(4, 5);
    Shape *p[2] = {&a, &b};
    long long s = 0;
    for (int i = 0; i < n; ++i) s += p[i & 1]->area();
    return s;
}

// The counter goes below zero: read at 32 bits by the test, at 64 by the sum.
long long down(int from, int to) {
    long long s = 0;
    for (int i = from; i > to; i -= 3) s += i;
    return s;
}

// A negative product compared at 32 bits, then widened on one path alone.
long long mixed(int a, int b) {
    int m = a * b - 7;
    int k = m ^ (a + b);
    if (k < m) return (long long)k * 2;
    return m;
}

// An unsigned sum that wraps: its low half is the value, the index reads it extended by zero.
unsigned wrap(unsigned x, unsigned y, const unsigned char *t) {
    unsigned u = x + y;
    unsigned v = u & 15u;
    if (u < x) return t[v] + 1000u;
    return t[v];
}

// A value carried round a loop and read only at 32 bits until the loop ends.
int carried(int n) {
    int h = -1;
    for (int i = 0; i < n; ++i) h = (int)((unsigned)h * 31u) ^ (i - 5);
    return h;
}

long long show(long long v) { return v; }

int main() {
    unsigned char t[16];
    for (int i = 0; i < 16; ++i) t[i] = (unsigned char)(i * 3);
    std::printf("virt %lld\n", virt(1001));
    std::printf("down %lld %lld\n", down(10, -20), down(-1, -2000000));
    std::printf("mixed %lld %lld %lld\n", mixed(-3, 5), mixed(40000, 50000), mixed(-46340, 46341));
    std::printf("wrap %u %u\n", wrap(4000000000u, 500000000u, t), wrap(7u, 6u, t));
    std::printf("carried %d %lld\n", carried(1000), show(carried(77)));
    int neg = carried(3);
    long long wide = neg;
    std::printf("wide %lld %lld\n", wide, (long long)(neg - 1) * 3);
    return 0;
}
