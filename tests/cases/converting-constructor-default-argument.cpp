// [class.conv.ctor]/1: a constructor whose parameters past the first all have defaults converts too - an
// argument passed by value or to a const reference, a copy-initialisation, a return and a cast, and a
// move-only argument the way `loopStatement(..., std::move(cond), ...)` passes an ExprPtr to an Effect.
extern "C" int printf(const char *, ...);
struct Own {
    int v;
    explicit Own(int k);
    Own(Own &&o);
    Own(const Own &) = delete;
};
Own::Own(int k) : v(k) {}
Own::Own(Own &&o) : v(o.v) { o.v = 0; }
struct Effect {
    int v; int guard;
    Effect(Own e = Own(1), int g = 7);
};
Effect::Effect(Own e, int g) : v(e.v), guard(g) {}
struct Num {
    int n; int k;
    Num(int a, int b = 3, int c = 4);
};
Num::Num(int a, int b, int c) : n(a * 10 + b), k(c) {}
static int byValue(Effect e) { return e.v * 100 + e.guard; }
static int byRef(const Num &m) { return m.n * 10 + m.k; }
static Num made(int x) { return x; }
int main() {
    Own a(5);
    const int one = byValue(static_cast<Own &&>(a));
    const int two = byValue(Own(6));
    const int three = byRef(2);
    Num m = 8;
    const Num r = made(9);
    const Num c = (Num)1;
    const Effect d;
    printf("%d %d %d %d %d %d %d %d %d\n", one, a.v, two, three, m.n + m.k, r.n, c.n, d.v, d.guard);
    return 0;
}
