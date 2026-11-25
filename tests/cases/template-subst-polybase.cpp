// M8.3 slice 3v: a class template over a polymorphic first base (std::thread's __thread_call over __thread_body), made from
// the read under -templates=subst: the base's table seeded, each override in its slot, the implicit destructor virtual.
#include "lifetime.h"

struct Body {
    virtual ~Body() {}
    virtual int run() = 0;
    virtual int twice() { return 2 * run(); }
};

struct Tracked {
    int v;
    Tracked(int x);
    Tracked(const Tracked &o);
    ~Tracked();
};
Tracked::Tracked(int x) : v(x) { lfBuilt(this, "Tracked"); }
Tracked::Tracked(const Tracked &o) : v(o.v) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }

template <class F> struct Call : Body {
    F f;
    explicit Call(const F &g) : f(g) {}
    int run() { return f.v + 1; }
};

template <class T> struct Plus : Body {
    T a, b;
    Plus(T x, T y) : a(x), b(y) {}
    int run() { return static_cast<int>(a + b); }
    int twice() { return static_cast<int>(a * b); }
};

int drive(Body *p) { return p->run() * 100 + p->twice(); }

int main() {
    lfWatch();
    Body *c = new Call<Tracked>(Tracked(4));
    Body *p = new Plus<short>(3, 5);
    Body *q = new Plus<char>(2, 7);
    printf("%d %d %d\n", drive(c), drive(p), drive(q));
    delete c;
    delete p;
    delete q;
    Call<Tracked> local(Tracked(9));
    printf("%d %d\n", local.run(), static_cast<Body &>(local).twice());
    return 0;
}
