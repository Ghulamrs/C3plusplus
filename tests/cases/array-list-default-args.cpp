// An array's braced list whose elements call a constructor completed by its default arguments:
// a local of a class with a destructor (the ledger), a class without one, and static arrays with every element written.
#include "lifetime.h"

struct D { int a, b; D(int a = 3, int b = 4); ~D(); };
D::D(int x, int y) : a(x), b(y) { lfBuilt(this, "D"); }
D::~D() { lfGone(this, "D"); printf("~D %d %d\n", a, b); }

struct P { int a, b; P(int a = 7, int b = 8); };
P::P(int x, int y) : a(x), b(y) {}

// Static storage outlives the ledger's report, so its class prints and keeps no ledger.
struct E { int a, b; E(int a = 3, int b = 4); ~E(); };
E::E(int x, int y) : a(x), b(y) {}
E::~E() { printf("~E %d %d\n", a, b); }

E sd[2] = { 9, 8 };
P sp[3] = { 1, 2, 5 };

int main() {
    lfWatch();
    {
        D d[1] = { 5 };
        printf("d %d %d\n", d[0].a, d[0].b);
        D e[3] = { 1, 2 };
        for (int i = 0; i < 3; i++) printf("e %d %d\n", e[i].a, e[i].b);
    }
    P p[2] = { 6 };
    for (int i = 0; i < 2; i++) printf("p %d %d\n", p[i].a, p[i].b);
    for (int i = 0; i < 2; i++) printf("sd %d %d\n", sd[i].a, sd[i].b);
    for (int i = 0; i < 3; i++) printf("sp %d %d\n", sp[i].a, sp[i].b);
    return 0;
}
