// A thrown class a constructor copies - a named object, a const one, and a temporary with a destructor: the copy
// constructor the parser chose builds the exception object, and the temporary dies before the throw leaves. The
// copies keep the value, so whether a compiler elides one does not show; the live count does. clang's output.
extern "C" int printf(const char *, ...);
static int live = 0;
struct E { int v; E(int x); E(const E &o); ~E(); };
E::E(int x) : v(x) { live++; }
E::E(const E &o) : v(o.v) { live++; }
E::~E() { live--; }
struct C { int v; C(int x); C(const C &o); };
C::C(int x) : v(x) {}
C::C(const C &o) : v(o.v) {}

static void throwTemp(int x) { throw E(x); }
static void throwNamed(int x) { E e(x + 1); throw e; }
static void throwConst(int x) { const C c(x * 2); throw c; }
static int catchE(int which, int x) {
    try {
        if (which == 0) throwTemp(x);
        else throwNamed(x);
    } catch (const E &e) {
        return e.v + live * 100;
    }
    return -1;
}
static int catchC(int x) {
    try { throwConst(x); } catch (const C &c) { return c.v; }
    return -1;
}
int main() {
    int a = catchE(0, 5);
    printf("%d %d\n", a, live);
    int b = catchE(1, 7);
    printf("%d %d\n", b, live);
    int c = catchC(3);
    printf("%d %d\n", c, live);
    return 0;
}
