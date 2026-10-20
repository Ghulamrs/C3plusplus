// alignas(N) on a local past the stack's 16 - [dcl.align], [basic.align] -
// and a by-value local of a class aligned past it: the frame is realigned to
// the widest slot, as clang's `and $-64, %rsp` does, and every other local,
// parameter and temporary of the function keeps its place in it. The
// addresses are printed modulo the alignment, never raw, and a call out of the
// realigned frame checks that the callee still finds its own arguments.
extern "C" int printf(const char *, ...);

struct alignas(32) Wide {
    int v[4];
    Wide(int a, int b) { v[0] = a; v[1] = b; v[2] = a + b; v[3] = a - b; }
    int sum() const { return v[0] + v[1] + v[2] + v[3]; }
};

static unsigned long bits(const void *p, unsigned long n) {
    return (unsigned long)p & (n - 1);
}

int fill(char *buf, int n, int seed) {
    for (int i = 0; i < n; i++) buf[i] = (char)(seed + i);
    return buf[n - 1];
}

int sixtyFour(int a, int b, int c) {
    alignas(64) char buf[48];
    int sum = a + b + c;
    const int last = fill(buf, 48, sum);
    printf("buf mod 64 = %lu, sum %d, last %d, first %d\n", bits(buf, 64), sum, last, buf[0]);
    return last + buf[0];
}

int byValue(int a) {
    Wide w(a, 3);
    alignas(128) double d = a * 1.5;
    Wide copy = w;
    copy.v[1] += 10;
    printf("w mod 32 = %lu, copy mod 32 = %lu, d mod 128 = %lu\n", bits(&w, 32), bits(&copy, 32), bits(&d, 128));
    printf("w %d copy %d d %g\n", w.sum(), copy.sum(), d);
    return w.sum() + copy.sum();
}

int main() {
    const int r = sixtyFour(1, 2, 3);
    const int s = byValue(5);
    printf("%d %d\n", r, s);
    return 0;
}
