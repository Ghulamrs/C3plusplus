// `throw;` through three handlers: each rethrows what it holds, the middle one after changing it, and an
// int rethrown from a handler nested in a try. x86_64-windows: _CxxThrowException(0, 0) from the funclet (M1).
extern "C" int printf(const char *, ...);
struct E { int v; E(int k); };
E::E(int k) : v(k) {}
static int depth = 0;
static int inner(int k) {
    try {
        if (k) throw E(k);
        return 0;
    } catch (const E &) {
        depth++;
        throw;
    }
}
static int middle(int k) {
    try {
        return inner(k);
    } catch (E &e) {
        e.v += 10;
        throw;
    }
}
static int outer(int k) {
    try {
        return middle(k);
    } catch (const E &e) {
        return e.v + 100;
    }
}
static int byInt(int k) {
    try {
        try { throw k; } catch (int) { throw; }
    } catch (int n) {
        return n * 2;
    }
}
int main() {
    printf("%d %d %d %d\n", outer(0), outer(5), depth, byInt(21));
    return 0;
}
