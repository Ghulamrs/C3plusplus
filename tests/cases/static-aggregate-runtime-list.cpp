// [stmt.dcl]/4: a static local or thread_local aggregate whose braced list is not constant, built the first time through.
#include <stdio.h>
struct P { int a, b; };
struct Q { char name[4]; int n; };
struct K { int v; K(int x) : v(x) {} };
int g(int x) { return x + 1; }
int f(int x) {
    static P ps[2] = { { g(x), 1 }, { 2, g(x + 2) } };
    static int grid[2][2] = { { g(x), 0 }, { 0, g(x + 3) } };
    static const int *ptrs[2] = { &ps[0].a, &grid[1][1] };
    static Q q = { "ab", g(x) };
    static K ks[2] = { K(g(x)), K(3) };
    static P one = { g(x), g(x) };
    return ps[0].a + ps[1].b + grid[1][1] + *ptrs[1] + q.name[1] + q.n + ks[0].v + one.b;
}

int h(int x) {
    thread_local P tp = { g(x), 7 };
    thread_local P tarr[2] = { { 1, g(x) }, { 3, 4 } };
    static union { int i; char c[4]; } u = { g(x) };
    static P cp = { 1, 2 };
    static P copy = cp;
    return tp.a + tp.b + tarr[0].b + u.i + copy.b;
}
int both() { return h(2) * 1000 + f(2); }
int main() { int a = f(1); int b = f(5); int c = h(4); int d = h(9); printf("%d %d %d %d %d\n", a, b, c, d, both()); return 0; }
