// The Itanium exception tail the SSA lowering takes (M3b.2): a handler that names
// no object - `catch (T)` and `catch (const T &)` with the name left out - and a
// noexcept function with landing pads of its own, whose callees never throw; and an
// ordinary function's try block, and a throw(...) list's filter over a body that
// throws what the list allows.
extern "C" int printf(const char *, ...);

struct E { int v; E(int x) : v(x) {} ~E() { v = 0; } };
struct D : E { D(int x) : E(x) {} };
struct R { int n; R(int v) : n(v) { printf("+R%d ", n); } ~R() { printf("-R%d ", n); } };

void raise(int k) {
    if (k == 1) throw 7;
    if (k == 2) throw E(2);
    if (k == 3) throw D(3);
    if (k == 4) throw 2.5;
}

int unnamed(int k) {
    int got = 0;
    try { raise(k); got = 1; }
    catch (int) { got = 10; }
    catch (const D &) { got = 30; }
    catch (E) { got = 20; }
    catch (...) { got = 40; }
    return got;
}

int nested(int k) {
    int got = 0;
    try {
        try { raise(k); }
        catch (double) { got = 4; throw; }
    } catch (double) { got += 100; }
    catch (int) { got = 1; }
    return got;
}

int seen = 0;
void tried(int k) try {
    R r(k);
    raise(k);
    seen = 1;
} catch (int e) {
    seen = 10 + e;
} catch (...) {
    seen = 50;
}

int counted(int k) try {
    raise(k);
    return 1;
} catch (int) {
    return 2;
}

int specified(int k) throw(int) {
    R r(k + 20);
    raise(k);
    return k;
}

int safe(int x) { return x * 2; }

int quiet(int x) noexcept {
    R r(x);
    int t = safe(r.n);
    { R s(t); t += s.n; }
    return t;
}

int main() {
    for (int k = 0; k <= 4; k++) {
        int u = unnamed(k);
        printf("| unnamed(%d)=%d\n", k, u);
    }
    for (int k = 0; k <= 4; k++) {
        if (k == 2 || k == 3) continue;
        int m = nested(k);
        printf("nested(%d)=%d\n", k, m);
    }
    for (int k = 0; k <= 2; k++) {
        tried(k);
        printf("| tried(%d) seen=%d\n", k, seen);
    }
    int c0 = counted(0), c1 = counted(1);
    printf("counted %d %d\n", c0, c1);
    for (int k = 0; k <= 1; k++) {
        try {
            int s = specified(k);
            printf("| specified(%d)=%d\n", k, s);
        } catch (int e) {
            printf("| specified(%d) threw %d\n", k, e);
        }
    }
    int q = quiet(5);
    printf("| quiet=%d\n", q);
    return 0;
}
