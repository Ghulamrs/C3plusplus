// The SSA lowering's caller side (M3b.2): a lambda's closure built in the frame, its scalar captures copied or bound,
// and a class prvalue - a plain class's temporary or one a constructor builds - bound to a const or an rvalue
// reference parameter, alive to the call's end.
extern "C" int printf(const char *, ...);

struct P { int x, y; };
struct C { int v; C(int a) : v(a * 10) {} };

template <class F> int apply(const F &f, int a) { return f(a); }
template <class F> int twice(F f, int a) { return f(f(a)); }
static int sum(const P &p) { return p.x + p.y; }
static int take(C &&c) { return c.v + 1; }
static int peek(const C &c) { return c.v; }

int main() {
    int k = 3, total = 0;
    double scale = 1.5;
    auto add = [k](int a) { return a + k; };
    auto bump = [&total](int a) { total += a; return total; };
    auto mix = [k, &total, scale](int a) { return static_cast<int>((a + k + total) * scale); };
    auto none = [](int a) { return a * 2; };
    printf("%d\n", add(4));
    bump(5);
    bump(6);
    printf("%d\n", total);
    printf("%d\n", mix(1));
    k = 100;
    printf("%d\n", add(4));
    printf("%d\n", apply([k](int a) { return a - k; }, 1));
    printf("%d\n", apply(none, 21));
    printf("%d\n", twice(add, 1));
    printf("%d\n", sum(P()));
    printf("%d\n", take(C(4)));
    printf("%d\n", peek(C(7)));
    return 0;
}
