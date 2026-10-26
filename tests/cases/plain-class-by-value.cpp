// A class with no constructor, destructor or copy of its own passed by value: the callee's parameter is its own copy,
// written without touching the caller's, of every size the two ABIs pass differently - in a register, in two, and in
// memory (M3b.2: what the SSA lowering does for such an argument, held against clang).
extern "C" int printf(const char *, ...);

struct One { int a; };
struct Two { long long a; double b; };
struct Big { int v[9]; char tag; };

static int one(One o) { o.a += 1; return o.a; }
static double two(Two t, int k) { t.a *= k; return static_cast<double>(t.a) + t.b; }
static int big(Big b) { int s = b.tag; for (int i = 0; i < 9; i++) { s += b.v[i]; b.v[i] = 0; } return s; }
static int mix(int x, Big b, One o, Two t) { return x + b.v[8] + o.a + static_cast<int>(t.b); }

int main() {
    One o = {41};
    Two t = {3, 0.5};
    Big b;
    for (int i = 0; i < 9; i++) b.v[i] = i * 10;
    b.tag = 7;
    const int r1 = one(o);
    const double r2 = two(t, 4);
    const int r3 = big(b);
    const int r4 = mix(1, b, o, t);
    printf("%d %d %g %lld %d %d %d\n", r1, o.a, r2, t.a, r3, b.v[3], r4);
    return 0;
}
