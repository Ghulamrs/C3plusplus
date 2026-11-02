// Class values where the SSA lowering wanted an object (M3b.2, round 24): a call's result of a class with a copy of
// its own in a slot of the frame; a derived temporary bound to a const reference to its base and a derived object
// sliced into a by-value parameter ([class.copy]/12); a converting constructor on the right of `=`; a braced list as an
// initializer_list argument - three, one and no elements; a scalar assignment named by an `int &` ([expr.ass]/1); a
// conditional of two void arms, each making a temporary; `reinterpret_cast<T &>` of an lvalue; a namespace-scope
// functor and a reference member functor called by their names; a union of scalars with a member initialiser passed
// and returned by value. Live objects are counted, never constructions: elision is the compiler's to choose.
#include <initializer_list>
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct Counted {
    int n;
    Counted() : n(0) {}
    Counted(const Counted &o) : n(o.n + 100) {}
};
static Counted give() { Counted c; c.n = 5; return c; }
static int peek(Counted c) { return c.n; }

struct B { int b; B(int x) : b(x) {} };
struct D : B { int d; D(int x) : B(x), d(x * 2) {} };
static int base(const B &r) { return r.b; }
static int sliced(B b) { return b.b * 10; }

struct It { int v; };
struct CIt { int v; CIt() : v(0) {} CIt(const It &i) : v(i.v + 100) {} };

static int total(std::initializer_list<int> xs) {
    int t = 0;
    for (const int *p = xs.begin(); p != xs.end(); ++p) t += *p;
    return t * 10 + (int)xs.size();
}

static int &assignRef(int &a, int b) { return a = b; }

struct T {
    int v;
    explicit T(int x) : v(x) { lfBuilt(this, "T"); }
    T(const T &o) : v(o.v) { lfBuilt(this, "T"); }
    ~T() { lfGone(this, "T"); }
};
static int seen;
static void note(const T &t) { seen += t.v; }

struct Scale {
    int by;
    int operator()(int n) const { return n * by; }
};
static Scale twice = { 2 };
struct Holder {
    const Scale &f;
    Holder(const Scale &s) : f(s) {}
    int apply(int n) const { return f(n); }
};

struct Ld { long double v; operator long double() const { return v * 2; } };

union U { int i = 3; double d; };
static U pass(U x) { return x; }

int main() {
    lfWatch();
    Counted r = give();
    printf("%d %d\n", r.n % 100, peek(r) % 100);
    printf("%d %d\n", base(D(4)), sliced(D(6)));
    It i = { 7 };
    CIt c;
    c = i;
    printf("%d\n", c.v);
    printf("%d %d %d\n", total({1, 2, 3}), total({7}), total({}));
    int a = 1;
    int &ra = assignRef(a, 9);
    ra += 1;
    printf("%d\n", a);
    {
        bool left = a > 5;
        left ? note(T(100)) : note(T(200));
        !left ? note(T(1)) : note(T(2));
        printf("%d\n", seen);
    }
    int word = 0x41424344;
    const char &first = reinterpret_cast<const char &>(word);
    printf("%d\n", first == 'D' || first == 'A');
    Holder h(twice);
    printf("%d %d\n", twice(21), h.apply(4));
    Ld ld = { 1.25L };
    long double got = ld;
    printf("%d\n", (int)(got * 4));
    U u;
    U back = pass(u);
    printf("%d\n", back.i);
    return 0;
}
