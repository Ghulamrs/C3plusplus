// A lambda inside a lambda, taking the captures of the one around it, and a class captured by value whose copy is its
// bytes though it has a constructor of its own: each closure built on the SSA path, under -front=ssa (M3b.2).
extern "C" { int printf(const char *, ...); }

struct P {
    int x;
    P(int a);
};

P::P(int a) { x = a * 2; }

int main(void) {
    int k = 3;
    int m = 10;
    P p(4);
    auto byValue = [k]() {
        auto inner = [k]() { return k + 1; };      // a copy of the outer copy
        return inner() * 2;
    };
    auto byReference = [&m]() {
        auto inner = [&m]() { m += 5; return m; };      // the outer reference, bound again
        return inner();
    };
    auto mixed = [=]() {
        auto inner = [=]() { return k * 100 + p.x; };
        return inner();
    };
    auto held = [p]() { return p.x + 1; };
    int r1 = byValue();
    int r2 = byReference();
    int r3 = mixed();
    int r4 = held();
    k = 50;
    int r5 = byValue();
    printf("%d %d %d %d %d %d\n", r1, r2, r3, r4, r5, m);
    return 0;
}
