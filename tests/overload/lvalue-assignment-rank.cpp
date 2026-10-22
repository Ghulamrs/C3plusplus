// **An assignment and a prefix `++` or `--` are lvalues** - [expr.ass]/1, [expr.pre.incr]/1 - so
// `const int &` takes them and `int &&` does not; a postfix `++` is a prvalue and goes to `int &&`.
// The compiler's own Walker.cpp writes `push_back(++nextIndex)` (M1 L12, 2026-10-23).
extern "C" { int printf(const char *, ...); }

static int pick(const int &a) { return 1 + 0 * a; }
static int pick(int &&a) { return 2 + 0 * a; }
static int ref(int &a) { return 3 + 0 * a; }
static int ref(const int &a) { return 4 + 0 * a; }

int main(void) {
    int x = 3;
    const int a = pick(++x);
    const int b = pick(x = 5);
    const int c = pick(x += 2);
    const int d = pick(--x);
    const int e = pick(x++);
    const int f = pick(x);
    const int g = ref(x = 1);
    const int h = ref(++x);
    printf("%d %d %d %d %d %d %d %d %d\n", a, b, c, d, e, f, g, h, x);
    return 0;
}
