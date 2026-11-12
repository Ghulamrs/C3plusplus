// A handler reading a value of the body (M3b.7 W1). On x86_64-windows a handler is a funclet: it runs on the
// parent's frame with none of its registers, so `this`, and anything else the body holds in a value, reaches it
// through a frame slot stored where the value is made. The Itanium targets read the value itself; the answers
// are the same on all four. clang's output; the Windows box runs it. No `return` in a handler.
#include "lifetime.h"
struct Tracked { int v; Tracked(int x); ~Tracked(); };
Tracked::Tracked(int x) : v(x) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }
struct Fail { int pos; bool hard; Fail(int p, bool h); };
Fail::Fail(int p, bool h) : pos(p), hard(h) {}
struct Probe {
    int at, fails, last;
    Tracked held;
    Probe();
    void note(int pos, const char *why);
    bool trial(int k);
    int sum(int n);
    int nested(int k);
    int twice(int k);
    void step(int k);
};
Probe::Probe() : at(0), fails(0), last(-1), held(7) {}
void Probe::note(int pos, const char *why) { fails++; last = pos; printf("note %d %s at %d\n", pos, why, at); }
void Probe::step(int k) { at++; if (k % 3 == 0) throw Fail(k, k % 2 == 0); }
// `this` read in the handler, a local of the body written there, and the handler's own values.
bool Probe::trial(int k) {
    const int save = at;
    bool ok = false;
    try {
        Tracked t(k);
        step(k);
        ok = true;
    } catch (const Fail &f) {
        if (f.hard) note(f.pos, "hard");
        ok = false;
        last = f.pos + held.v;
    }
    at = save;
    return ok;
}
// A try in a loop: the handler reads `this` every turn, and a member through it.
int Probe::sum(int n) {
    int total = 0;
    for (int i = 1; i <= n; i++) {
        try { step(i); total += i; } catch (const Fail &f) { total -= f.pos; fails += at; }
    }
    return total;
}
// A handler inside a handler: the inner funclet reads `this` and the outer handler's caught object.
int Probe::nested(int k) {
    int r = 0;
    try {
        step(k);
        r = 1;
    } catch (const Fail &f) {
        try {
            step(f.pos * 3);
            r = 2;
        } catch (const Fail &g) {
            r = g.pos + f.pos + fails;
            note(g.pos, "inner");
        }
        r += at;
    }
    return r;
}
// Two handlers of one function, and a third after them: one slot serves them all.
int Probe::twice(int k) {
    int r = 0;
    try { step(k); } catch (const Fail &f) { r += f.pos + last; }
    try { step(k + 3); } catch (const Fail &) { r += 100 * fails; } catch (...) { r = -1; }
    try { throw 5; } catch (int e) { r += e + at; }
    return r;
}
int main() {
    lfWatch();
    {
        Probe p;
        const bool a = p.trial(1);
        const bool b = p.trial(3);
        const bool c = p.trial(6);
        printf("%d %d %d\n", a, b, c);
        printf("at %d fails %d last %d\n", p.at, p.fails, p.last);
        const int s = p.sum(7);
        printf("sum %d fails %d\n", s, p.fails);
        const int n2 = p.nested(2);
        const int n3 = p.nested(3);
        printf("nested %d %d\n", n2, n3);
        const int t3 = p.twice(3);
        const int t4 = p.twice(4);
        printf("twice %d %d\n", t3, t4);
        printf("at %d fails %d last %d\n", p.at, p.fails, p.last);
    }
    return 0;
}
