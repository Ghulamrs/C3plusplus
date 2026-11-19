// [class.nest]/3 under the object ledger: a nested class defined outside its class, with constructors and a
// destructor defined out of line through both qualifiers, held as a member of its enclosing class and made
// as a local, an array and a temporary - each built once and destroyed once.
extern "C" int printf(const char *, ...);
#include "lifetime.h"

struct Outer {
    struct Part;
    struct Part *first;
    int n;
    Outer();
    ~Outer();
};

struct Outer::Part {
    int v;
    Part();
    explicit Part(int v);
    Part(const Part &o);
    ~Part();
};
Outer::Part::Part() : v(1) { lfBuilt(this, "Part"); }
Outer::Part::Part(int w) : v(w) { lfBuilt(this, "Part"); }
Outer::Part::Part(const Part &o) : v(o.v + 10) { lfBuilt(this, "Part"); }
Outer::Part::~Part() { lfGone(this, "Part"); }

struct Holder {
    Outer::Part p;
    Holder();
    ~Holder();
};
Holder::Holder() : p(7) { lfBuilt(this, "Holder"); }
Holder::~Holder() { lfGone(this, "Holder"); }

Outer::Outer() : first(0), n(2) { lfBuilt(this, "Outer"); }
Outer::~Outer() { lfGone(this, "Outer"); }

static int read(Outer::Part p) { return p.v; }

int main() {
    lfWatch();
    Outer o;
    Outer::Part a(3);
    Outer::Part row[2];
    Holder h;
    int byValue = read(a);
    int temp = Outer::Part(5).v;
    printf("%d %d %d %d %d %d\n", o.n, a.v, row[1].v, h.p.v, byValue, temp);
    return 0;
}
