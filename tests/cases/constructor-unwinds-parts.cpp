// [except.ctor]/3 through the SSA lowering (M3b.2): a constructor that has built some of its bases and
// members when a later one, or its body, throws destroys those, last built first - and a return, a try
// that catches or a local of the body's own leaves them built. Each throw is caught in main.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
int limit = 100;
struct P { int v; P(int x); ~P(); };
P::P(int x) : v(x) { if (v >= limit) throw v; lfBuilt(this, "P"); printf("+P%d ", v); }
P::~P() { printf("-P%d ", v); lfGone(this, "P"); }
struct B1 { B1(); ~B1(); };
B1::B1() { lfBuilt(this, "B1"); printf("+B1 "); }
B1::~B1() { printf("-B1 "); lfGone(this, "B1"); }
struct B2 { int k; B2(int x); ~B2(); };
B2::B2(int x) : k(x) { if (x >= limit) throw x; lfBuilt(this, "B2"); printf("+B2 "); }
B2::~B2() { printf("-B2 "); lfGone(this, "B2"); }
struct Plain { int n; };
struct Two : B1, B2 { int s; P a; Plain q; P b; Two(int x, int y, int z); };
Two::Two(int x, int y, int z) : B2(x), s(0), a(y), b(z) {
    printf("body ");
    if (z == 4) return;
    P local(z + 1);
    if (z == 5) throw z;
    try { if (z == 6) throw 60; } catch (int e) { printf("caught%d ", e); }
    s = local.v;
}
// No throw and no try of its own: what the Microsoft ABI lowers too, its cleanups funclets.
struct Three : B1 { P a; P b; Three(int y, int z); };
Three::Three(int y, int z) : a(y), b(z) { printf("body3 "); }
void make3(int y, int z) {
    try {
        Three t(y, z);
        printf("made3 ");
    } catch (int e) {
        printf("threw%d ", e);
    }
    printf("\n");
}
void make(int x, int y, int z) {
    try {
        Two t(x, y, z);
        printf("made%d ", t.s);
    } catch (int e) {
        printf("threw%d ", e);
    }
    printf("\n");
}
int main() {
    lfWatch();
    make(1, 2, 3);
    make(100, 2, 3);
    make(1, 200, 3);
    make(1, 2, 300);
    make(1, 2, 4);
    make(1, 2, 5);
    make(1, 2, 6);
    make3(1, 2);
    make3(1, 200);
    make3(100, 2);
    limit = 3;
    make(1, 2, 2);
    return 0;
}
