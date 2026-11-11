// **Selection hands SSA values to MIR's allocator** (M3b.4 round 7): a value's slot is a local the allocator may give a
// register, read in its low bytes by a narrower move; a frame object's address, a constant and null are made at each use;
// a branch's arguments are a parallel move, a swap among them its cycle. Each shape here printed against clang's answer.
#include <stdarg.h>
extern "C" int printf(const char *, ...);

struct Pair { int a; int b; };
struct Noisy { int v; Noisy(int x); ~Noisy(); };
Noisy::Noisy(int x) : v(x) { printf("+%d ", v); }
Noisy::~Noisy() { printf("-%d ", v); }

int twice(int x) { return x * 2; }
int sumPair(Pair p) { return p.a * 10 + p.b; }
int bump(int *p) { return ++*p; }

// Narrow values: a char, a short and an unsigned read from 8-byte slots.
int narrow(signed char c, short s, unsigned u) {
    signed char d = static_cast<signed char>(c + 1);
    short t = static_cast<short>(s * 3);
    unsigned w = u + 7u;
    return d + t + static_cast<int>(w % 100u);
}

// A swap carried round a loop: the back edge passes (b, a) to (a, b), a cycle the parallel move must break.
int swaps(int n) {
    int a = 1, b = 2, k = 0;
    for (int i = 0; i < n; i++) { int t = a; a = b; b = t; k += a * 3 + b; }
    return k * 100 + a * 10 + b;
}

// Values live across calls, a constant used after each, a local's address handed out.
int across(int x) {
    int y = twice(x) + 5;
    int z = twice(y) + 5;
    int cell = x;
    bump(&cell);
    bump(&cell);
    return y + z + cell + 5;
}

int vsum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int (*pick(int which))(int) { return which ? twice : 0; }

// A cleanup that unwinds, and a value read after the handler.
int guarded(int x) {
    int before = x + 1;
    try {
        Noisy n(before);
        if (x > 2) throw x * 3;
        return before;
    } catch (int e) {
        return e + before;
    }
}

int main() {
    Pair p = {3, 4};
    printf("%d %d %d\n", narrow(-3, 7, 4000000000u), narrow(100, -2, 9u), sumPair(p));
    printf("%d %d %d\n", swaps(0), swaps(3), swaps(4));
    printf("%d %d\n", across(1), across(-4));
    printf("%d %d\n", vsum(3, 1, 2, 3), vsum(0));
    int (*f)(int) = pick(1);
    printf("%d %d\n", f(21), pick(0) == 0);
    printf("%d\n", guarded(1));
    printf("%d\n", guarded(5));
    return 0;
}
