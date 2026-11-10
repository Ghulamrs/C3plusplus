// Dead code, dead stores and CFG simplification on SSA (M3b.4 round 3): branches and a switch on a folded constant,
// the blocks they leave unreached, a chain of jumps merged, empty blocks forwarded, values and loop-carried parameters
// nothing reads, stores a handler's slot never shows, and a try on a path never taken. Its -O1 -dump=ssa=cfg and
// -dump=ssa=dead on x86_64-linux are ssa-dead.cfg and ssa-dead.dead, which tests/ssa.sh holds.
#include <cstdio>

static int mayThrow(int x) {
    if (x < 0) throw x;
    return x + 1;
}

static int constant(int x) {
    int mode = 2;
    int r = x;
    if (mode == 2) r = x * 3; else r = x - 100;        // the false arm is never reached
    switch (mode + 1) {
    case 1: r += 1; break;
    case 3: r += 30; break;
    default: r += 7; break;
    }
    if (mode > 5) { try { mayThrow(x); } catch (int e) { std::printf("never %d\n", e); } }
    return r;
}

static int unused(int x, int n) {
    int waste = x * 17 + 3;                            // read by nothing
    int carried = 0, t = 0;
    for (int i = 0; i < n; i++) { carried = carried + i * waste; t += i; }   // carried: a loop parameter nothing reads
    if (x > 0) { } else { }                            // two empty arms, one place
    return t + x;
}

static int handler(int x) {
    int seen = 1;                                      // overwritten before anything can read it
    seen = 2;
    try {
        seen = mayThrow(x);
        seen = mayThrow(seen) + 10;
    } catch (int e) {
        std::printf("caught %d after %d\n", e, seen);
    }
    seen = 5;                                          // the slot's last store: nothing reads it after
    return x + 5;
}

static int chain(int x) {
    int a = x;
    if (x > 100) goto one;
    a += 1;
one:
    goto two;
two:
    a *= 2;
    goto three;
three:
    return a;
}

int main() {
    std::printf("%d %d\n", constant(4), constant(-2));
    std::printf("%d %d\n", unused(3, 4), unused(-1, 0));
    std::printf("%d %d\n", handler(2), handler(-7));
    std::printf("%d %d\n", chain(5), chain(200));
    return 0;
}
