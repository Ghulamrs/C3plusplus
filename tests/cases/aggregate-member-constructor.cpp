// [dcl.init.aggr]: an aggregate whose member's class has constructors is still an aggregate - the
// implicit constructors this makes do not count - and a braced list initialises it member by
// member, the member copy-initialised from its value (a converting constructor runs), from a
// list of its constructor's arguments, or value-initialised where the list ends. cpp11 took the
// braces as arguments to the aggregate's own constructor and refused `{ v, 3 }`. Found compiling
// the COM Core sources (`Constant backN = { back };`, a table of `Turn`).
extern "C" int printf(const char *, ...);

struct V {
    int a;
    V() : a(-1) {}
    V(int x) : a(x) {}
    V(int x, int y) : a(x * 10 + y) {}
};
struct A { V v; int k; };
struct B { int k; V v; V w; };
struct C { A in; V more[2]; };

A g[2] = { { V(1), 2 }, { 3, 4 } };
const B h = { 5, V(6) };
static C c = { { 7, 8 }, { 9 } };

int main() {
    V seed(2);
    A a = { seed, 3 };
    B b = { 1, 4 };
    B pair = { 1, { 2, 3 } };
    A arr[3] = { { 7, 1 } };
    printf("%d %d | %d %d %d | %d | %d %d %d %d\n", a.v.a, a.k, b.k, b.v.a, b.w.a, pair.v.a,
           arr[0].v.a, arr[1].v.a, arr[2].k, arr[2].v.a);
    printf("%d %d %d %d | %d %d %d | %d %d %d %d\n", g[0].v.a, g[0].k, g[1].v.a, g[1].k, h.k, h.v.a, h.w.a,
           c.in.v.a, c.in.k, c.more[0].a, c.more[1].a);
    return 0;
}
