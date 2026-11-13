// `volatile` through MIR (M3 review R8): the selector marks the instruction of each volatile load and store, and no
// MIR pass removes, forwards, folds, narrows or hoists one. Each function is a shape a pass would otherwise take:
// a read nobody uses (remove-dead), a store read back (forward-values), a read fed to an add (fold-loads), a store
// nothing reads (remove-dead-stores), a double read in a loop (hoist-xmm-slots), an 8-byte read used at 4 (shrink).
// Every object has internal linkage or none: cl decorates a variable's name with its cv (volatile-object.notarget).

extern "C" int printf(const char *, ...);

static volatile int port = 3;

static int unread() {
    volatile int v = 1;
    v;
    v;
    return 2;
}

static int readBack() {
    volatile int v;
    v = 5;
    return v + v;
}

static int addend(int x) {
    return x + port + port;
}

static int unreadStore() {
    volatile int w = 1;
    w = 2;
    w = 3;
    return 4;
}

static double inLoop() {
    volatile double step = 0.5;
    double sum = 0;
    for (int i = 0; i < 4; i++) sum += step;
    return sum;
}

static int narrowed() {
    volatile long long wide = 0x100000007LL;
    return static_cast<int>(wide) + static_cast<int>(wide);
}

int main() {
    printf("%d %d %d %d\n", unread(), readBack(), addend(1), unreadStore());
    printf("%g %d %d\n", inLoop(), narrowed(), static_cast<int>(port));
    return 0;
}
