// M3b.5 round 2: calls cloned in place on the SSA IR - a leaf in a loop, a callee with a switch and one with a loop of
// its own, a void callee writing through a pointer, a callee that calls, one called under a destructor's region, a
// self call; and the ones refused by name: a callee that throws, one with a handler, one returning a class.
#include <cstdio>

struct Pair { int a, b; };

struct Guard {
    int id;
    explicit Guard(int n);
    ~Guard();
};
Guard::Guard(int n) : id(n) { std::printf("guard %d\n", id); }
Guard::~Guard() { std::printf("~guard %d\n", id); }

static int square(int x) { return x * x; }

static int kind(int x) {
    switch (x % 4) {
    case 0: return 10;
    case 1: return 20;
    case 2: return 30;
    default: return 40;
    }
}

static int sumTo(int n) {
    int s = 0;
    for (int i = 1; i <= n; i++) s += i;
    return s;
}

static void bump(int *p, int by) { *p += by; }

static int twice(int x) { return square(x) + square(x + 1); }

static const char *name(int x) { return x > 2 ? "big" : "small"; }

static int depth(int n) { return n <= 0 ? 0 : 1 + depth(n - 1); }

static int risky(int x) {
    if (x > 100) throw x;
    return x + 1;
}

static int guarded(int x) {
    try {
        return risky(x);
    } catch (int e) {
        std::printf("caught %d\n", e);
    }
    return -1;
}

static Pair make(int a, int b) {
    Pair p;
    p.a = a;
    p.b = b;
    return p;
}

static double half(double d) { return d / 2; }

static unsigned char low(int x) { return static_cast<unsigned char>(x); }

int main() {
    int total = 0;
    for (int i = 0; i < 6; i++) {
        total += square(i);
        total += kind(i);
    }
    std::printf("total %d\n", total);
    std::printf("sumTo %d %d\n", sumTo(10), sumTo(0));
    int cell = 5;
    bump(&cell, 7);
    bump(&cell, square(3));
    std::printf("cell %d\n", cell);
    std::printf("twice %d\n", twice(4));
    std::printf("name %s %s\n", name(1), name(9));
    std::printf("depth %d\n", depth(5));
    {
        Guard g(1);
        int in = square(7) + kind(6);
        std::printf("in %d\n", in);
        std::printf("risky %d\n", risky(41));
    }
    std::printf("guarded %d %d\n", guarded(3), guarded(200));
    Pair p = make(3, 4);
    std::printf("pair %d %d\n", p.a, p.b);
    std::printf("half %.2f low %d\n", half(5.0), low(300));
    return 0;
}
