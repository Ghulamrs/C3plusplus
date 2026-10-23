// A `try` owes, on the way out, the objects of the block it is a statement of: that block splits its cleanup
// region around it. A block further out does not, and its region destroyed its objects again after the `try`'s
// chain, or its handler's end-catch pad, had already destroyed them - once per object, for a `try` in an `if`
// or a loop, and an exception escaping it (M1: stage 2 re-entered its refusal from a freed exception object).
#include <cstdio>

static int built, gone;
struct Obj {
    int v;
    Obj(int n) : v(n) { built++; }
    ~Obj() { gone++; }
};
struct Done {};

static void thrower(int k) {
    if (k == 0) throw 1;
    if (k == 1) throw 2.5;
}

static int unmatched(int loop) {
    Obj outer(1);
    if (loop) {
        Obj inner(2);
        try { thrower(1); } catch (int) { return 1; }
    }
    return 0;
}

static int fromHandler() {
    Obj outer(3);
    for (int k = 0; k < 2; k++) {
        Obj step(k);
        try {
            Obj trial(4);
            thrower(0);
        } catch (int) {
            throw Done();
        }
    }
    return 0;
}

static int direct() {
    Obj outer(5);
    try { thrower(1); } catch (int) { return 1; }
    return 0;
}

static int bare(int c) {
    Obj outer(6);
    if (c) try { thrower(1); } catch (int) { return 1; }
    return 0;
}

int main() {
    try { unmatched(1); } catch (double) { std::printf("unmatched %d\n", built - gone); }
    try { fromHandler(); } catch (const Done &) { std::printf("from handler %d\n", built - gone); }
    try { direct(); } catch (double) { std::printf("direct %d\n", built - gone); }
    try { bare(1); } catch (double) { std::printf("bare %d\n", built - gone); }
    std::printf("%d %d\n", built, gone);
    return 0;
}
