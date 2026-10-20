// A throw through a frame realigned for an over-aligned local: the unwinder
// restores the base register the frame is addressed from (rbx, x19) from the
// CFI the prologue wrote, so the cleanup that runs on the way out still finds
// the local, and so does the handler below it. The ledger counts the object.
#include "lifetime.h"

struct Held {
    int id;
    Held(int i) : id(i) { lfBuilt(this, "Held"); }
    ~Held() { printf("~Held %d\n", id); lfGone(this, "Held"); }
};

void thrower(int n) {
    if (n > 1) throw n;
}

int realigned(int n) {
    alignas(64) int wide[16];
    Held h(n);
    wide[0] = n;
    wide[15] = n * 2;
    thrower(n);
    return wide[0] + wide[15];
}

int main() {
    lfWatch();
    printf("%d\n", realigned(1));
    try {
        alignas(32) char pad[32];
        pad[0] = 'x';
        realigned(7);
        printf("not reached %c\n", pad[0]);
    } catch (int e) {
        printf("caught %d\n", e);
    }
    return 0;
}
