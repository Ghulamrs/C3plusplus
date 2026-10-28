// A handler's own work on the Microsoft ABI, under -front=ssa: a local with a destructor inside a handler - left by
// falling off the end, by a throw to an enclosing try, by a return and by a goto - is a cleanup funclet of its own
// whose state chains to the handler's; a goto out of a handler, forward or back, leaves the funclet through an exit
// the try's pad lists, and one to a label inside the handler leaves nothing. Live objects are counted, never
// constructions, and every call is a statement of its own. The Itanium targets take their own path; clang's output.
extern "C" int printf(const char *, ...);
static int live = 0;
struct A { int v; A(int x); ~A(); };
A::A(int x) : v(x) { live++; }
A::~A() { live--; }
static void boom(int x) { throw x; }

static int fell(int k) {
    int n = 0;
    try { boom(k); } catch (int e) { A a(2); n = e + a.v; }
    return n * 100 + live;
}
static int rethrown(int k) {
    int n = 0;
    try {
        try { boom(k); } catch (int e) { A a(3); A b(4); boom(e + a.v + b.v); }
    } catch (int e) { n = e; }
    return n * 100 + live;
}
static int returned(int k) {
    try { boom(k); } catch (int e) { A a(5); return e + a.v; }
    return -1;
}
static int forward(int k) {
    A outer(1);
    try { boom(k); } catch (int e) { A a(6); if (e > 1) goto out; printf("small "); }
    printf("after ");
out:
    return live;
}
static int backward(int k) {
    int turns = 0;
again:
    try { boom(turns); } catch (int e) { A a(7); if (++turns < k) goto again; printf("turns%d ", e); }
    return turns * 100 + live;
}
static int inside(int k) {
    int n = 0;
    try { boom(k); } catch (int e) { A a(8); if (e) goto skip; n = 1; skip: n += a.v; }
    return n * 100 + live;
}
int main() {
    int r = fell(1); printf("%d\n", r);
    r = rethrown(2); printf("%d\n", r);
    r = returned(3); printf("%d\n", r);
    r = forward(1); printf("%d\n", r);
    r = forward(5); printf("%d\n", r);
    r = backward(3); printf("%d\n", r);
    r = inside(0); printf("%d\n", r);
    r = inside(1); printf("%d\n", r);
    printf("live %d\n", live);
    return 0;
}
