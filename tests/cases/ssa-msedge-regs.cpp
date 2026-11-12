// Values the allocator holds in registers across a Microsoft funclet's edges: computed before a nested try and read after
// the outer handler's continuation, a rethrow and a cleanup's throw from an inner handler into the outer one, and a loop
// whose body catches on some turns and goes on with its counters.
extern "C" int printf(const char *, ...);

static int gone = 0, base = 3;

struct Guard {
    ~Guard();
};

Guard::~Guard() { gone++; }

static int thrower(int what) {
    if (what == 1) throw 7;
    if (what == 2) throw 2.5;
    return what * 10;
}

static int rethrown(int x, int what) {
    const int a = x * base + 1, b = x * x - base, c = a ^ b, d = a + 2 * b;
    int r = 0;
    try {
        try {
            r = thrower(what);
        } catch (int) {
            r = 1;
            throw;
        }
    } catch (...) {
        r += a + b;
    }
    return r * 1000 + a + b * 3 + c * 5 + d * 7;
}

static int cleanupOut(int x, int what) {
    const int a = x + base * 4, b = x * 3 - 1, c = a * b;
    int r = 2;
    try {
        try {
            thrower(1);
        } catch (int) {
            Guard g;
            r = 4;
            thrower(what);
            r = 8;
        }
    } catch (double) {
        r += c;
    }
    return r + a * 11 + b * 13 + c;
}

static int loopCatches(int n) {
    const int k = n * 7 + base;
    int acc = 0, caught = 0, sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i * k;
        try {
            acc += thrower(i % 3 == 1 ? 1 : i + 3);
        } catch (int) {
            caught++;
        }
        sum += k - i;
    }
    return acc + caught * 100000 + sum * 3 + k;
}

int main() {
    const int r1 = rethrown(5, 1), r2 = rethrown(6, 2), r3 = rethrown(7, 4);
    printf("%d %d %d\n", r1, r2, r3);
    const int c1 = cleanupOut(5, 2), c2 = cleanupOut(9, 0);
    printf("%d %d %d\n", c1, c2, gone);
    const int l1 = loopCatches(10), l2 = loopCatches(1);
    printf("%d %d\n", l1, l2);
    return 0;
}
