// [stmt.for]/1 with an init-statement declaring an object that has a destructor: it lives to the loop's end, which
// destroys it - after the last turn, on a break, on a return from the body and when an exception leaves the loop. The
// ledger balances under -front=ssa (M3b.2), the init hoisted in front of the loop as the parser wrote it.
#include "lifetime.h"

struct Turn {
    int left;
    Turn(int n);
    ~Turn();
    bool more();
};
Turn::Turn(int n) : left(n) { lfBuilt(this, "Turn"); }
Turn::~Turn() { lfGone(this, "Turn"); }
bool Turn::more() { return left-- > 0; }

static int counted(int n) {
    int k = 0;
    for (Turn t(n); t.more(); k++) {}
    return k;
}

static int broken(int n) {
    int k = 0;
    for (Turn t(n); t.more(); k++)
        if (k == 2) break;
    return k;
}

static int returned(int n) {
    for (Turn t(n); t.more(); )
        if (t.left == 1) return 100 + t.left;
    return -1;
}

static void thrower(int k) { if (k == 3) throw k; }

static int unwound(int n) {
    int k = 0;
    try {
        for (Turn t(n); t.more(); k++) thrower(k);
    } catch (int e) {
        return e * 10 + k;
    }
    return k;
}

int main() {
    lfWatch();
    printf("%d %d\n", counted(4), counted(0));
    printf("%d %d\n", broken(5), broken(1));
    printf("%d %d\n", returned(3), returned(0));
    printf("%d %d\n", unwound(6), unwound(2));
    return 0;
}
