// A class with a destructor and no copy of its own, returned by value - lowered to SSA under -front=ssa: a local
// the function built handed out with no destructor run on it while every other live object is destroyed, a
// temporary built for the return, a member, a global and a conditional's object copied out before the destructors
// run - none of it a choice between elisions; and placement new of such a class, whose constructor alone runs.
#include <new>
extern "C" int printf(const char *, ...);

struct D {
    int n;
    D(int v);
    ~D();
};
D::D(int v) : n(v) { printf("  +D %d\n", n); }
D::~D() { printf("  ~D %d\n", n); n = -1; }

D global(70);

D fromLocal(int k) {
    D other(k + 100);
    D d(k);
    return d;
}

D fromInner(int k) {
    D outer(k + 200);
    {
        D inner(k);
        if (k > 0) return inner;
    }
    return D(k);
}

D fromTemporary(int k) {
    D before(k + 300);
    return D(k * 2);
}

D fromGlobal() {
    D local(5);
    return global;
}

D pick(bool first) {
    D a(11), b(22);
    return first ? a : b;
}

D inLoop(int stop) {
    for (int i = 0; i < 5; i++) {
        D t(i);
        if (i == stop) return t;
    }
    return D(-9);
}

struct Holder {
    D d;
    Holder(int v) : d(v) {}
    D get() const { return d; }
};

static double store[4];
int placed(int v) {
    D *p = new (store) D(v);
    const int n = p->n;
    p->~D();
    return n;
}

int placedBeside(int v) {
    D keep(v + 1);
    D *p = new (store) D(v);
    const int n = p->n + keep.n;
    p->~D();
    return n;
}

int main() {
    { D r = fromLocal(1); printf("fromLocal %d\n", r.n); }
    { D r = fromInner(2); printf("fromInner %d\n", r.n); }
    { D r = fromTemporary(3); printf("fromTemporary %d\n", r.n); }
    { D r = fromGlobal(); printf("fromGlobal %d\n", r.n); }
    { D r = pick(true); printf("pick %d\n", r.n); }
    { D r = pick(false); printf("pick %d\n", r.n); }
    { D r = inLoop(2); printf("inLoop %d\n", r.n); }
    { D r = inLoop(7); printf("inLoop %d\n", r.n); }
    { Holder h(40); D r = h.get(); printf("get %d\n", r.n); }
    const int p = placed(55);
    printf("placed %d\n", p);
    const int q = placedBeside(60);
    printf("placedBeside %d\n", q);
    return 0;
}
