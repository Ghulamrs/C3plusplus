// [namespace.memdef]/2 under the object ledger: a class declared in a namespace and defined from outside it,
// constructors and a destructor defined out of line through the namespace, made as a local, an array, a member
// of another class and a temporary - each built once and destroyed once.
extern "C" int printf(const char *, ...);
#include "lifetime.h"

namespace lib { struct Part; }

struct lib::Part {
    int v;
    Part();
    explicit Part(int v);
    Part(const Part &o);
    ~Part();
};
lib::Part::Part() : v(1) { lfBuilt(this, "Part"); }
lib::Part::Part(int w) : v(w) { lfBuilt(this, "Part"); }
lib::Part::Part(const Part &o) : v(o.v + 10) { lfBuilt(this, "Part"); }
lib::Part::~Part() { lfGone(this, "Part"); }

struct Holder {
    lib::Part p;
    Holder();
    ~Holder();
};
Holder::Holder() : p(7) { lfBuilt(this, "Holder"); }
Holder::~Holder() { lfGone(this, "Holder"); }

static int read(lib::Part p) { return p.v; }

int main() {
    lfWatch();
    lib::Part a(3);
    lib::Part row[2];
    Holder h;
    int byValue = read(a);
    int temp = lib::Part(5).v;
    printf("%d %d %d %d %d\n", a.v, row[1].v, h.p.v, byValue, temp);
    return 0;
}
