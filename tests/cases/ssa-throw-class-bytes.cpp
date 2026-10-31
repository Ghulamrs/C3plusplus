// [except.throw]/3 for a class whose copy is its bytes: the exception object takes them from an object, from a temporary
// a constructor built - destroyed after the copy, before the throw - and from a value-initialised plain class; the runtime
// destroys the object at the catch's end. Lowered under -front=ssa (M3b.2); the destructors count, never print.
extern "C" int printf(const char *, ...);

static int ended;
struct Base { int v; Base(int n); };
struct Derived : Base { int w; Derived(int n); };
struct Marked { int v; Marked(int n); ~Marked(); };
struct Empty {};
struct Plain { int a, b; };
Base::Base(int n) : v(n) {}
Derived::Derived(int n) : Base(n), w(n * 2) {}
Marked::Marked(int n) : v(n) {}
Marked::~Marked() { ended++; }

static int byBase(int k) {
    try { if (k) throw Derived(k); }
    catch (const Base &b) { return b.v; }
    return 0;
}

static int byDerived(int k) {
    Derived d(k);
    d.w = 7;
    try { throw d; }
    catch (Derived &e) { return e.v * 10 + e.w; }
    return 0;
}

static int marked(int k) {
    try { throw Marked(k); }
    catch (Marked &m) { return m.v; }
    return 0;
}

static int empty() {
    try { throw Empty(); }
    catch (Empty &) { return 1; }
    return 0;
}

static int plain(int k) {
    try { throw Plain(); }
    catch (const Plain &p) { return p.a + p.b + k; }
    return 0;
}

int main() {
    printf("%d %d\n", byBase(4), byBase(0));
    printf("%d\n", byDerived(3));
    int m = marked(5);
    printf("%d %d\n", m, ended > 0);
    printf("%d %d\n", empty(), plain(9));
    return 0;
}
