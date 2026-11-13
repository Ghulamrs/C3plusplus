// A handler's local destroyed when an exception leaves a try nested in that handler (Itanium, the walker): the inner
// try's unmatched chain hands over to the outer handler's end-catch pad and skips the local's cleanup, so `live` ends 2
// where clang ends 0. -front=ssa answers clang's; found 2026-10-28 by ssa-try-in-handler.
extern "C" int printf(const char *, ...);
static int live = 0;
struct A { int v; A(int x); ~A(); };
A::A(int x) : v(x) { live++; }
A::~A() { live--; }
static void boom(int x) { throw x; }
static void boomd(double x) { throw x; }

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
int main() {
    int a = rethrownOut(5);
    printf("%d\n", a);
    int b = pastInner(6);
    printf("%d\n", b);
    printf("live %d\n", live);
    return 0;
}
