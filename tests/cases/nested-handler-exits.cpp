// Every way out of a handler nested in another, on every target: a return, a break and a continue that leave
// both, and a break that stays inside the outer one. On x86_64-windows each handler is a funclet, and an exit
// from the inner one lands in the outer's funclet and leaves it in turn (M1).
#define LF_BALANCE_ONLY
#include "lifetime.h"
struct E { int v; E(int n); E(const E &o); ~E(); };
E::E(int n) : v(n) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }
static int retTwo() {
    try { throw E(2); }
    catch (E &a) { try { throw E(3); } catch (E &b) { return a.v * 10 + b.v; } }
    return 0;
}
static int breakTwo() {
    int n = 0;
    for (int i = 0; i < 5; i++) {
        try { throw E(i); }
        catch (E &a) {
            n += a.v;
            try { throw E(100); }
            catch (E &b) { if (a.v == 2) break; n += 1; }
        }
    }
    return n;
}
static int contTwo() {
    int n = 0;
    for (int i = 0; i < 4; i++) {
        try { throw E(i); }
        catch (E &a) {
            try { throw E(7); }
            catch (E &b) { if (a.v % 2) continue; n += b.v + a.v; }
            n += 1000;
        }
    }
    return n;
}
static int breakInside() {
    int n = 0;
    try { throw E(1); }
    catch (E &a) {
        for (int i = 0; i < 9; i++) {
            try { throw E(i); }
            catch (E &b) { if (b.v == 3) break; n += b.v; }
        }
        n += a.v * 100;
    }
    return n;
}
int main() {
    lfWatch();
    int a = retTwo();
    int b = breakTwo();
    int c = contTwo();
    int d = breakInside();
    printf("%d %d %d %d\n", a, b, c, d);
    return 0;
}
