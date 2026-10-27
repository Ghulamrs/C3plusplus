// A local with a destructor on the Microsoft ABI, lowered to SSA: its cleanup a funclet and a state of the FH3 tree,
// chained to the enclosing local's, run by the runtime when a call in the region throws (M3b.2, M3-IR 3.6). The
// throwing and catching functions take the parser's path there - a try and a throw stay outside - so what is measured
// is the lowered function's funclets: one local, two in one scope, nested scopes, a loop body's, and an early return.
extern "C" int printf(const char *, ...);

static int gone = 0;

struct Noisy {
    int v;
    Noisy(int x) : v(x) { printf("+%d ", v); }
    ~Noisy() { gone++; printf("-%d ", v); }
};

static int boom(int x) {
    if (x > 2) throw x;
    return x;
}

static int one(int x) {
    Noisy a(1);
    return boom(x) + a.v;
}

static int two(int x) {
    Noisy a(10);
    Noisy b(20);
    int r = boom(x);
    return r + a.v + b.v;
}

static int nested(int x) {
    Noisy a(100);
    int r = 0;
    {
        Noisy b(200);
        r = boom(x);
        {
            Noisy c(300);
            r += boom(x + 1) + c.v;
        }
        r += b.v;
    }
    return r + a.v;
}

static int loop(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        Noisy t(i);
        if (i == 1) continue;
        sum += boom(i) + t.v;
    }
    return sum;
}

static int early(int x) {
    Noisy a(7);
    if (x == 0) return a.v;
    Noisy b(8);
    if (x == 1) return b.v;
    return boom(x) + a.v + b.v;
}

static int guarded(int x, int (*f)(int)) {
    try {
        return f(x);
    } catch (int e) {
        printf("caught %d ", e);
        return -e;
    }
}

int main() {
    printf("%d\n", guarded(1, one));
    printf("%d\n", guarded(3, one));
    printf("%d\n", guarded(2, two));
    printf("%d\n", guarded(4, two));
    printf("%d\n", guarded(1, nested));
    printf("%d\n", guarded(2, nested));
    printf("%d\n", guarded(5, nested));
    printf("%d\n", guarded(2, loop));
    printf("%d\n", guarded(4, loop));
    printf("%d\n", guarded(0, early));
    printf("%d\n", guarded(1, early));
    printf("%d\n", guarded(2, early));
    printf("%d\n", guarded(9, early));
    printf("gone %d\n", gone);
    return 0;
}
