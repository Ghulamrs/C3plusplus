// M3b.2: a plain class returned by value from a body lowered to SSA - the address of the object returned, copied out
// by the ABI's rule: in registers where it fits (one of 8, of 16, of two doubles) and through the result pointer where not.
extern "C" int printf(const char *, ...);

struct Small { int a; int b; };
struct Pair { double x; double y; };
struct Big { int v[6]; long long tag; };
struct Byte { char c; };

static Small makeSmall(int a) { Small s; s.a = a; s.b = a * 3; return s; }
static Pair makePair(double x) { Pair p = { x, x / 2 }; return p; }
static Big makeBig(int k) {
    Big b;
    for (int i = 0; i < 6; i++) b.v[i] = k + i;
    b.tag = 1000000000000LL + k;
    return b;
}
static Byte makeByte(char c) { Byte b = { c }; return b; }
static Small pick(const Small &l, const Small &r, bool first) {
    if (first) return l;
    return r;
}

struct Maker {
    int base;
    Big big(int k) const { Big b = makeBig(0); b.v[0] = base + k; return b; }
};

int main() {
    Small s = makeSmall(4);
    Pair p = makePair(3.0);
    Big b = makeBig(7);
    Byte c = makeByte('q');
    Small q = pick(s, makeSmall(9), false);
    Maker m = { 100 };
    Big g = m.big(5);
    printf("%d %d %g %g\n", s.a, s.b, p.x, p.y);
    printf("%d %d %lld %c\n", b.v[0], b.v[5], b.tag, c.c);
    printf("%d %d %d %d\n", q.a, q.b, g.v[0], g.v[1]);
    return 0;
}
