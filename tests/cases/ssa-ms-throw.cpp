// A throw on the Microsoft ABI, lowered to SSA: the object in a slot of the frame and _CxxThrowException with the
// ThrowInfo chain the parser finished for its type (M3b.2, M3-IR 3.6). The catching function takes the parser's path
// there - a try stays outside - so what is measured is the lowered throwers: a scalar, a converted one, a pointer, a
// plain class by its bytes, a throw past a local with a destructor (an invoke in its cleanup's region), and a rethrow.
extern "C" int printf(const char *, ...);

static int gone = 0;

struct Noisy {
    int v;
    Noisy(int x) : v(x) { printf("+%d ", v); }
    ~Noisy() { gone++; printf("-%d ", v); }
};

struct Plain { int a; double b; };

static int one(int x) {
    if (x > 2) throw x;
    return x;
}

static double converted(int x) {
    if (x > 2) throw x * 1.5;
    return x;
}

static int pointer(const char *s) {
    if (s[0] == 'x') throw s;
    return s[0];
}

static int plain(int x) {
    Plain p;
    p.a = x;
    p.b = x / 2.0;
    if (x > 2) throw p;
    return p.a;
}

static int guarded(int x) {
    Noisy a(1);
    if (x > 2) throw x + 100;
    return a.v + x;
}

static int nested(int x) {
    Noisy a(10);
    {
        Noisy b(20);
        if (x > 2) throw 'c';
    }
    return a.v + x;
}

static void again() {
    throw;
}

static int rethrown(int x) {
    try {
        throw x;
    } catch (int) {
        again();
    }
    return 0;
}

int main() {
    try { printf("%d ", one(1)); printf("%d ", one(5)); } catch (int e) { printf("int %d\n", e); }
    try { printf("%g ", converted(1)); printf("%g ", converted(5)); } catch (double e) { printf("double %g\n", e); }
    try { printf("%d ", pointer("ab")); printf("%d ", pointer("xy")); } catch (const char *e) { printf("ptr %s\n", e); }
    try { printf("%d ", plain(2)); printf("%d ", plain(6)); } catch (Plain e) { printf("plain %d %g\n", e.a, e.b); }
    try { printf("%d ", guarded(2)); printf("%d ", guarded(5)); } catch (int e) { printf("guarded %d gone %d\n", e, gone); }
    try { printf("%d ", nested(2)); printf("%d ", nested(5)); } catch (char e) { printf("nested %c gone %d\n", e, gone); }
    try { rethrown(9); } catch (int e) { printf("rethrown %d\n", e); }
    return 0;
}
