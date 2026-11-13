// A class whose array members have more elements than a compiler would copy one call at a time: its implicit default
// constructor, copy constructor, copy assignment and destructor ([class.copy]/15, /28: each element, in order).
#include "lifetime.h"

struct W { int v; W(); W(const W &o); W &operator=(const W &o); ~W(); };
static int serial = 0, assigned = 0;
W::W() : v(serial++) { lfBuilt(this, "W"); }
W::W(const W &o) : v(o.v + 1000) { lfBuilt(this, "W"); }
W &W::operator=(const W &o) { v = o.v + 5000; assigned++; return *this; }
W::~W() { lfGone(this, "W"); }

struct C { int v; C(); C(const C &o); C &operator=(const C &o); ~C(); };
static int cBuilt = 0, cCopied = 0, cAssigned = 0, cGone = 0;
C::C() : v(cBuilt++) {}
C::C(const C &o) : v(o.v + 1) { cCopied++; }
C &C::operator=(const C &o) { v = o.v + 2; cAssigned++; return *this; }
C::~C() { cGone++; }

struct N { int v; N(); N(const N &o); };
static int nCopied = 0;
N::N() : v(7) {}
N::N(const N &o) : v(o.v + 1) { nCopied++; }

struct H { W a[17]; W b[65]; C c[1000]; N n[65]; N m[3][30]; int tag; };

static int sumW(const W *w, int n) { int s = 0; for (int i = 0; i < n; i++) s += w[i].v; return s; }
static int sumC(const C *c, int n) { int s = 0; for (int i = 0; i < n; i++) s += c[i].v; return s; }
static int sumN(const N *p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i].v; return s; }

static void show(const char *what, const H &h) {
    printf("%s: a %d b %d c %d n %d m %d tag %d\n", what, sumW(h.a, 17), sumW(h.b, 65), sumC(h.c, 1000), sumN(h.n, 65), sumN(h.m[0], 90), h.tag);
}

int main()
{
    lfWatch();
    {
        H h;
        h.tag = 1;
        show("built", h);
        H k(h);
        show("copied", k);
        H j;
        j.tag = 2;
        j = h;
        show("assigned", j);
        printf("first and last: %d %d %d %d\n", k.a[0].v, k.a[16].v, k.b[0].v, k.b[64].v);
    }
    printf("W assigned %d; C built %d copied %d assigned %d gone %d; N copied %d\n", assigned, cBuilt, cCopied, cAssigned, cGone, nCopied);
    return 0;
}
