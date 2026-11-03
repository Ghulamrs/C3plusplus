// Constructors the SSA lowering builds (M3b.2, round 24): a delegating constructor ([class.base.init]/6), its target
// chosen with default arguments, and one whose body throws once the target has returned - the whole object destroyed
// ([except.ctor]/2); a member and a base built by a constructor with default arguments ([dcl.fct.default]/9). Live
// objects are counted, never constructions.
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct Part {
    int v;
    Part(int a, int b = 20) : v(a + b) { lfBuilt(this, "Part"); }
    ~Part() { lfGone(this, "Part"); }
};

struct Whole {
    Part p;
    int n;
    Whole(int a, int b = 3) : p(a), n(a * b) { lfBuilt(this, "Whole"); }
    Whole() : Whole(5) {}
    Whole(bool fail) : Whole(7, 2) { if (fail) throw 9; }
    ~Whole() { lfGone(this, "Whole"); }
};

struct Base {
    int b;
    Base(int x, int y = 100) : b(x + y) {}
};
struct Derived : Base {
    Part q;
    Derived(int x) : Base(x), q(x) {}
};

int main() {
    lfWatch();
    {
        Whole w;
        printf("%d %d\n", w.p.v, w.n);
        Whole k(false);
        printf("%d %d\n", k.p.v, k.n);
    }
    int caught = 0;
    try {
        Whole bad(true);
        printf("%d\n", bad.n);
    } catch (int e) {
        caught = e;
    }
    printf("%d\n", caught);
    Derived d(4);
    printf("%d %d\n", d.b, d.q.v);
    return 0;
}
