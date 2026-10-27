// Default arguments completing calls of every kind the SSA lowering takes: a free function, a member, a static
// member, one in a namespace, a default that calls a function or reads a global, and one past a written argument.
extern "C" int printf(const char *, ...);

int base = 40;
int seed() { return 2; }

int add(int a, int b = 5, int c = 7) { return a * 100 + b * 10 + c; }
double scale(double x, double by = 2.5) { return x * by; }
int fromGlobal(int a = base + 1) { return a; }
int fromCall(int a = seed() * 3) { return a; }
const char *label(const char *s = "none") { return s; }
long long wide(int a, long long b = 7000000000LL) { return b + a; }

namespace ns {
const int k = 9;
int twice(int a = k) { return a * 2; }
}

struct Acc {
    int v;
    int bump(int by = 3) { v += by; return v; }
    static int scaled(int a, int f = 4) { return a * f; }
    int both(int a = 1, int b = 2) const { return v + a * b; }
};

int main() {
    printf("%d %d %d\n", add(1), add(1, 2), add(1, 2, 3));
    printf("%.2f %.2f\n", scale(4.0), scale(4.0, 0.5));
    printf("%d %d %s %s\n", fromGlobal(), fromCall(), label(), label("given"));
    printf("%lld\n", wide(5));
    printf("%d %d\n", ns::twice(), ns::twice(4));
    Acc a;
    a.v = 10;
    int first = a.bump();
    int second = a.bump(7);
    printf("%d %d\n", first, second);
    printf("%d %d %d\n", Acc::scaled(5), a.both(), a.both(3));
    base = 100;
    printf("%d\n", fromGlobal());
    return 0;
}
