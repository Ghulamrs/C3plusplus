// Control-flow shapes the SSA lowering takes (M3b.2 round 24): a class declared in an `if`'s condition and read by its
// conversion function, alive to the statement's end ([stmt.select]/3-4); a `while` declaring a scalar each turn; a
// range-based `for` copying a plain class out of an array; `p->S::~S()` named through the object's own class, beside a
// qualified member call `b->B::get()` that must stay a call; and `typeid` of a type and of a polymorphic object.
#define LF_BALANCE_ONLY      // a copy elided or not
#include "lifetime.h"
#include <typeinfo>

struct Flag {
    int v;
    Flag(int x) : v(x) { lfBuilt(this, "Flag"); }
    Flag(const Flag &o) : v(o.v) { lfBuilt(this, "Flag"); }
    ~Flag() { lfGone(this, "Flag"); if (v >= 100) printf("~Flag %d\n", v); }     // the condition's object alone
    operator bool() const { return v > 2; }
};

struct Pt { int x, y; };

struct B {
    int k;
    int get() const { return k * 10; }
    virtual ~B() {}
};
struct D : B { int extra; };

struct S {
    int v;
    ~S() { lfGone(this, "S"); }
};

static int left = 4;
static int next() { return left--; }

static int qualified(const B *bp) { return bp->B::get(); }     // a member call, not a destructor

static int test(int x) {
    int r = 0;
    if (Flag f = Flag(x)) {
        printf("then %d\n", f.v);
        r = f.v;
        f.v += 100;
    } else {
        printf("else %d\n", f.v);
        r = -f.v;
        f.v += 100;
    }
    printf("after if\n");
    return r;
}

int main() {
    lfWatch();
    printf("%d\n", test(5));
    printf("%d\n", test(1));

    int sum = 0;
    while (int n = next()) sum += n;
    printf("sum %d\n", sum);

    Pt pts[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };
    int t = 0;
    for (Pt p : pts) { t += p.x * p.y; p.x = 0; }
    printf("pts %d %d\n", t, pts[0].x);

    B b;
    b.k = 7;
    printf("get %d\n", qualified(&b));

    S *s = static_cast<S *>(static_cast<void *>(new char[sizeof(S)]));
    s->v = 1;
    lfBuilt(s, "S");
    s->S::~S();
    delete[] static_cast<char *>(static_cast<void *>(s));

    D d;
    B &br = d;
    printf("typeid %d %d %d\n", typeid(br) == typeid(D), typeid(b) == typeid(B), typeid(int) == typeid(long));
    return 0;
}
