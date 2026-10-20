// [class.static.data]/3: a `static constexpr` data member of any literal type takes its
// initialiser in the class - a class with a constexpr constructor, braces, an aggregate, a
// double - and a constant expression reads it there. cpp11 took only a `static const` integer.
// It is laid down as a weak definition, so a read at run time finds it with or without the
// definition outside the class, which may still be written (`constexpr P S::k;`).
extern "C" int printf(const char *, ...);

struct P { int x, y; constexpr P(int a, int b) : x(a), y(b) {} };
struct Ag { int a; double b; };
struct S {
    static constexpr P k = P(4, 5);
    static constexpr P k2{6, 7};
    static constexpr double d = 2.5;
    static constexpr Ag ag = { 3, 1.25 };
    static constexpr int n = k.x + k2.y;
    static constexpr int twice(int v) { return v * 2; }
};
constexpr P S::k;
template <class T> struct W {
    static constexpr T v = T(3, 9);
    static constexpr double h = 0.5;
    static constexpr int one() { return 1; }
};
static_assert(S::k.y == 5 && S::k2.x == 6 && S::d == 2.5 && S::ag.b == 1.25 && S::n == 11, "");
static_assert(W<P>::v.y == 9 && W<P>::h == 0.5 && W<P>::one() == 1 && S::twice(4) == 8, "");
const P *addr() { return &S::k; }

int main() {
    double d = S::d;
    printf("%d %d %g %d %g %d %d %g %d\n", S::k.x, addr()->y, d, S::ag.a, S::ag.b, S::n, W<P>::v.x,
           W<P>::h, S::twice(W<P>::one()));
    return 0;
}
