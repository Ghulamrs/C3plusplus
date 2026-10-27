// M3b.2: [except.throw] lowered to SSA on the Itanium ABI - a scalar and a plain class thrown, caught in the same
// function and in a caller, and `throw;` passing the one being handled on.
extern "C" int printf(const char *, ...);

struct Err { int code; int line; };

static int checked(int v) {
    if (v < 0) throw v;
    if (v > 100) { Err e = { v, 12 }; throw e; }
    return v * 2;
}
static int local(int v) {
    int got = 0;
    try {
        if (v == 3) throw 3.5;
        got = v;
    } catch (double d) {
        got = (int)(d * 10);
    }
    return got;
}
static int passOn(int v) {
    try {
        return checked(v);
    } catch (int) {
        throw;
    }
}

int main() {
    int a = 0, b = 0, c = 0, d = 0;
    try { a = checked(5); checked(-7); } catch (int x) { b = x; }
    try { checked(150); } catch (Err e) { c = e.code + e.line; }
    try { passOn(-2); } catch (int x) { d = x * 100; }
    printf("%d %d %d %d %d %d\n", a, b, c, d, local(3), local(4));
    return 0;
}
