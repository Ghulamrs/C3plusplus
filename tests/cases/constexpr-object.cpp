// [dcl.constexpr]/9: a `constexpr` object of literal class type, its value worked out while
// compiling from a constexpr constructor's initialiser list - bases, members, a member's own
// constructor, a default member initialiser - or an aggregate's braces. Read back by a constant
// expression (static_assert, an array bound, a case label), by a constexpr member function, and
// at run time; one at namespace scope is laid down as data and no constructor runs for it.
extern "C" int printf(const char *, ...);

struct In { int a; constexpr In(int x) : a(x * 2) {} };
struct P {
    int x, y; In in; double d; bool b; char c;
    constexpr P(int a, int b2) : x(a), y(b2 + a), in(b2), d(1.5 * a), b(a > 1), c('A' + a) {}
    constexpr int sum() const { return x + y + in.a; }
    constexpr P shifted(int k) const { return P(x + k, y); }
};
constexpr P mk(int k) { return P(k, k + 1); }
constexpr int use(P p) { return p.sum(); }
constexpr double half(P p) { return p.d / 2; }
struct Ag { int a; char b; double c; };
struct B0 { int z; constexpr B0(int v) : z(v) {} };
struct B1 { int w; constexpr B1() : w(7) {} };
struct D : B0, B1 {
    int k = 3; int m;
    constexpr D(int a) : B0(a), m(a + k) {}
    constexpr int all() const { return z + w + k + m; }
};
struct E { int e; constexpr E(int v) : e(v) {} constexpr E(const E &o) : e(o.e + 100) {} };
// Declared constexpr in the class, defined outside it.
struct O { int v; constexpr O(int a); };
constexpr O::O(int a) : v(a + 1) {}

constexpr P gp(2, 5);
constexpr D gd(4);
constexpr Ag gag = { 9, 'z', 2.5 };
constexpr O go(2);
constexpr int fromGlobal = gp.sum() + gd.all() + gag.a;
constexpr int parens(5);
int arr[gp.x + gd.m];

int main() {
    constexpr P p(1, 2);
    constexpr P q = p;
    constexpr P r = mk(3);
    static_assert(p.x == 1 && p.y == 3 && p.in.a == 4, "p");
    static_assert(p.sum() == 8 && use(p) == 8, "sum");
    static_assert(q.y == 3 && r.y == 7 && mk(4).x == 4, "copies");
    static_assert(p.b == false && r.b == true && p.c == 'B', "bool char");
    static_assert(half(r) == 2.25, "double");
    static_assert(p.shifted(10).x == 11, "member returning object");
    constexpr Ag ag = { 1, 'c' };
    static_assert(ag.a == 1 && ag.b == 'c' && ag.c == 0, "aggregate");
    constexpr D d(10);
    static_assert(d.z == 10 && d.w == 7 && d.k == 3 && d.m == 13 && d.all() == 33, "bases");
    constexpr E e1(1);
    constexpr E e2 = e1;
    static_assert(e2.e == 101, "user copy constructor");
    static_assert(fromGlobal == 49 && go.v == 3 && parens == 5, "globals");
    int local[p.sum()];
    printf("%d %d %d %d %g %d %d %d %d %d\n", (int)sizeof local, (int)sizeof arr, fromGlobal, gp.y,
           gp.d, gd.all(), gag.b, e2.e, d.all(), go.v);
    switch (3) { case p.y: printf("case\n"); }
    return 0;
}
