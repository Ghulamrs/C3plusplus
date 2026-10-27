// A member's own initialiser in a written constructor that leaves it out, beside one the list names: scalars, one
// reading another member and a global, a pointer, a braced value, a plain class copied, and a class template's.
extern "C" int printf(const char *, ...);

int start = 30;
struct Pt { int x; int y; };
Pt origin = { 4, 5 };

struct S {
    int a = 7;
    int b = a + start;
    double d = 1.5;
    const char *s = "dflt";
    long long w{9000000000LL};
    Pt p = origin;
    S() {}
    S(int given) : a(given) {}
    S(int given, double e) : b(given), d(e) {}
};

template <class T> struct Box {
    T v = T(3);
    int n = 11;
    Box() {}
    explicit Box(int k) : n(k) {}
};

void show(const S &x) {
    printf("%d %d %.2f %s %lld %d %d\n", x.a, x.b, x.d, x.s, x.w, x.p.x, x.p.y);
}

int main() {
    S one;
    S two(2);
    S three(3, 0.25);
    show(one);
    show(two);
    show(three);
    start = 100;
    S four;
    show(four);
    Box<int> b1;
    Box<double> b2(5);
    printf("%d %d %.2f %d\n", b1.v, b1.n, b2.v, b2.n);
    return 0;
}
