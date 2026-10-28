// A try inside a catch handler on the Microsoft ABI, under -front=ssa: the inner try's dispatch state chains to the
// handler's (or to a cleanup opened inside the handler before it), its handlers are funclets of their own that come
// back into the outer handler's funclet, and the try map lists the inner try before the outer. What is thrown out of
// the inner handler, or past the inner try, reaches the try outside the handler. Live objects are counted, never
// constructions, and every call is a statement of its own. The Itanium targets take their own path; clang's output.
extern "C" int printf(const char *, ...);
static int live = 0;
struct A { int v; A(int x); ~A(); };
A::A(int x) : v(x) { live++; }
A::~A() { live--; }
static void boom(int x) { throw x; }
static void boomd(double x) { throw x; }

static int inner(int k) {
    int n = 0;
    try { boom(k); } catch (int e) {
        n = e;
        try { boomd(2.5); } catch (double d) { n = n * 10 + static_cast<int>(d); }
        n = n * 10 + 7;
    }
    return n * 100 + live;
}
static int innerLocals(int k) {
    int n = 0;
    try { boom(k); } catch (int e) {
        A before(1);
        try { boomd(e + 0.5); } catch (double d) { A in(2); n = static_cast<int>(d) + in.v + live; }
        A after(3);
        n = n * 10 + after.v + before.v + live;
    }
    return n * 100 + live;
}
static int rethrownOut(int k) {
    int n = 0;
    try {
        try { boom(k); } catch (int e) {
            A a(4);
            try { boomd(1.5); } catch (double d) { A b(5); boom(e + a.v + b.v + static_cast<int>(d)); }
            n = -1;
        }
    } catch (int e) { n = e; }
    return n * 100 + live;
}
static int pastInner(int k) {
    int n = 0;
    try {
        try { boom(k); } catch (int e) {
            A a(6);
            try { boom(e + a.v); } catch (double d) { n = -static_cast<int>(d); }
            n = -1;
        }
    } catch (int e) { n = e; }
    return n * 100 + live;
}
static int noThrowInside(int k) {
    int n = 0;
    try { boom(k); } catch (int e) {
        try { n = e + 1; } catch (double d) { n = -static_cast<int>(d); }
        n = n * 10 + 3;
    }
    return n * 100 + live;
}
static int brokeOut(int k) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        try { boom(k + i); } catch (int e) {
            try { boomd(0.5); } catch (double d) { n = n * 10 + e + static_cast<int>(d); }
            if (e == k + 1) break;
        }
    }
    return n * 100 + live;
}
int main() {
    printf("%d %d %d\n", inner(3), innerLocals(4), rethrownOut(5));
    printf("%d %d %d\n", pastInner(6), noThrowInside(7), brokeOut(8));
    printf("live %d\n", live);
    return 0;
}
