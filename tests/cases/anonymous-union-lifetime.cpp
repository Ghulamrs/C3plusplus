// A class with a written destructor and a member with a destructor beside an
// anonymous union, under the object ledger: the union's members are scalars
// and neither the constructor nor the destructor has anything to run for
// them, and the member beside them is still built once and destroyed once.
// A copy constructor names one member of the union in its list - the one
// place [class.base.init]/8 allows - and the copy is built from it.
extern "C" int printf(const char *, ...);
#include "lifetime.h"

struct Tracked {
    int v;
    Tracked();
    ~Tracked();
};
Tracked::Tracked() : v(11) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }

struct Owner {
    int kind;
    union { int i; double d; };
    Tracked t;
    Owner();
    Owner(const Owner &o);
    ~Owner();
};
Owner::Owner() : kind(1), d(2.5) { lfBuilt(this, "Owner"); }
Owner::Owner(const Owner &o) : kind(o.kind), d(o.d), t() { lfBuilt(this, "Owner"); }
Owner::~Owner() { lfGone(this, "Owner"); }

int main() {
    lfWatch();
    {
        Owner a;
        Owner b(a);
        a.i = 7;
        printf("%d %g %d %d %d\n", b.kind, b.d, b.t.v, a.i, a.t.v);
    }
    return 0;
}
