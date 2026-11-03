// [class.bit] through the SSA lowering (M3b.2 round 25): a bit-field read and written - signed, unsigned, bool, an
// enumeration and 40 bits wide - beside its neighbours in one unit, through `.`, `->` and `this`, by `=` and `op=`, and an
// assignment's value the field's, truncated to its width.
extern "C" int printf(const char *, ...);
enum Colour { Red, Green, Blue };
struct F {
    int s : 5;
    unsigned u : 3;
    bool b : 1;
    Colour c : 3;
    unsigned long long w : 40;
    int tail;
    int bump() { s += 3; u |= 4; return s * 10 + (int)u; }
};
static int readAll(const F *p) { return p->s + (int)p->u + (p->b ? 100 : 0) + (int)p->c * 1000; }
int main() {
    F f;
    f.tail = 77;
    f.s = -3;
    f.u = 9;
    f.b = true;
    f.c = Blue;
    f.w = 0x12345678ABULL;
    printf("%d %u %d %d %llx %d\n", f.s, (unsigned)f.u, (int)f.b, (int)f.c, f.w, f.tail);
    int v = (f.u = 13);
    printf("%d %u\n", v, (unsigned)f.u);
    int k = f.bump();
    printf("%d %d %d\n", k, readAll(&f), f.tail);
    f.w += 1;
    f.s -= 9;
    f.b = false;
    printf("%llx %d %d %d\n", f.w, f.s, readAll(&f), f.tail);
    return 0;
}
