// Arrays initialised from braced lists under the SSA lowering (M3b.2): the parser counts a declarator's written
// bounds, so `int a[] = {1}` is one value and not a bound; nested arrays, plain classes, unions and strings in a list
// are placed as a class's aggregate is ([dcl.init.aggr]/11 brace elision, /16 a union's first member, [dcl.init.string]).
#include <stdio.h>

struct P { int x; int y; };
union U { int i; float f; };
struct Name { char text[4]; int n; };
struct Row { P cells[2]; char tag[3]; };

static int one() { int a[] = {1}; return a[0]; }
static int sole() { int a[1] = {7}; return a[0]; }
static int two(int k) { int b[2] = {2}; return b[k]; }

static int nested() {
    int m[2][3] = { { 1, 2, 3 }, { 7, 8, 9 } };
    int n[2][2] = { 1, 2, 3 };
    long l[2][2] = { { 10 }, { 30, 40 } };
    double d[2][3] = { { 0.5 }, { 1.5, 2.5 } };
    int s = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++) s = s * 3 + m[i][j];
    return s + n[0][1] * 100 + n[1][0] * 1000 + n[1][1] * 10000 + (int)(l[0][1] + l[1][1]) + (int)(d[1][1] * 4 + d[0][2]);
}

static int strings() {
    char g[2][4] = { "ab", "cde" };
    char big[80] = "hi";
    Name nm = { "pqr", 3 };
    Name ns[2] = { { "x", 1 }, { "yz", 2 } };
    return g[0][1] + g[1][2] * 2 + big[1] + big[79] + nm.text[2] + nm.n + ns[1].text[1] + ns[1].n + ns[0].text[2];
}

static int classes() {
    P ps[3] = { { 1, 2 }, { 3, 4 } };
    P q[2] = { 5, 6, 7 };
    P r[] = { { 2, 0 }, { 1, 1 }, { 9, 8 } };
    U u[2] = { { 1 }, { 2 } };
    Row rows[2] = { { { { 1, 2 }, { 3, 4 } }, "ab" }, { 5, 6, 7, 8, "c" } };
    return ps[1].y + ps[2].x * 10 + q[1].x * 100 + q[1].y * 1000 + (int)(sizeof r / sizeof r[0]) + r[2].y +
           u[1].i * 7 + rows[1].cells[1].y * 3 + rows[0].tag[1] + rows[1].tag[0];
}

static long tail(int k) {
    long z[100] = { 4, 5 };
    return z[k] + z[1];
}

int main() {
    printf("%d %d %d %d\n", one(), sole(), two(0), two(1));
    printf("%d\n", nested());
    printf("%d\n", strings());
    printf("%d\n", classes());
    printf("%ld %ld\n", tail(0), tail(99));
    return 0;
}
