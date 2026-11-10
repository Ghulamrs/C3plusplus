// `volatile` on SSA (M3b.4 round 6): every load and store of an object written volatile is marked by the lowering, and
// no SSA pass promotes, forwards, merges or drops one. Each function below is a shape a pass would otherwise take: a
// local read three times (promote, gvn), stores no load sees (dead), a global read twice in a block (gvn), a member, a
// whole object, an array, a loop counter, a parameter. Its -O1 -dump=ssa=dead is recorded beside it (ssa-volatile.dead).
// Every object here has internal linkage or none: cl decorates a variable's name with its cv (volatile-object.notarget).

extern "C" int printf(const char *, ...);

static volatile int ticks = 0;

struct Port { int plain; volatile int reg; };
struct Pair { int a, b; };

static int byValue(volatile int n) {
    n = n + 1;
    return n;
}

static int reads() {
    volatile int v = 7;
    int a = v, b = v, c = v;
    return a + b + c;
}

static int stores() {
    volatile int w;
    w = 1;
    w = 2;
    volatile int never = 5;
    return w;
}

static int twice() {
    ticks = ticks + 1;
    int first = ticks;
    int second = ticks;
    return first + second;
}

static int member() {
    Port p;
    p.plain = 3;
    p.reg = 4;
    p.reg = 5;
    return p.plain + p.reg;
}

static int wholeObject() {
    volatile Pair q;
    q.a = 1;
    q.b = 2;
    return q.a + q.b;
}

static int array() {
    volatile char pad[4];
    for (int i = 0; i < 4; i++) pad[i] = static_cast<char>(i + 1);
    return pad[0] + pad[3];
}

static int counter() {
    int sum = 0;
    for (volatile int i = 0; i < 3; i++) sum += i;
    return sum;
}

int main() {
    const volatile int frozen = 11;
    printf("%d %d %d %d\n", reads(), stores(), twice(), member());
    printf("%d %d %d %d\n", wholeObject(), array(), counter(), byValue(41));
    printf("%d %d\n", frozen, ticks);
    return 0;
}
