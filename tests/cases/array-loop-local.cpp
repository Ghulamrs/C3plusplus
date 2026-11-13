// A local array of a class with more elements than a compiler would build one call at a time: [dcl.init]/7 each element
// first to last, [class.dtor]/10 last built first - 17 and 65 on the ledger, 1000 counted; with and without a destructor,
// with default arguments, in two dimensions, from a list with a long tail, and left early by return and continue.
#include "lifetime.h"

struct W { int v; W(); W(int x); ~W(); };
static int serial = 0;
W::W() : v(serial++) { lfBuilt(this, "W"); }
W::W(int x) : v(x) { lfBuilt(this, "W"); }
W::~W() { lfGone(this, "W"); }

// Counted, the ledger holding 512: `inOrder` counts the destructions that took the last one built.
struct C { int v; C(); ~C(); };
static int cBuilt = 0, cGone = 0, inOrder = 0;
C::C() : v(cBuilt++) {}
C::~C() { if (v == cBuilt - 1 - cGone) inOrder++; cGone++; }

struct N { int v; N(); };
static int nBuilt = 0;
N::N() : v(nBuilt++) {}

struct D { int v; D(int a = 3, int b = 4); ~D(); };
D::D(int a, int b) : v(a * b) { lfBuilt(this, "D"); }
D::~D() { lfGone(this, "D"); }

static int sum17() { W a[17]; int s = 0; for (int i = 0; i < 17; i++) s += a[i].v; return s; }
static int sum65() { W a[65]; int s = 0; for (int i = 0; i < 65; i++) s += a[i].v; return s; }
static int sum1000() { C c[1000]; int s = 0; for (int i = 0; i < 1000; i++) s += c[i].v; return s; }
static int sumPlain() { N n[65]; N m[1000]; int s = 0; for (int i = 0; i < 65; i++) s += n[i].v; return s + m[0].v + m[999].v; }
static int sumDefaults() { D d[20]; int s = 0; for (int i = 0; i < 20; i++) s += d[i].v; return s; }
static int sumGrid() { W g[3][6]; int s = 0; for (int i = 0; i < 3; i++) for (int j = 0; j < 6; j++) s += g[i][j].v; return s; }
static int sumList() { W a[40] = { 100, 200, 7 }; int s = 0; for (int i = 0; i < 40; i++) s += a[i].v; return s; }
static int sumListDefaults() { D d[30] = { D(5, 6) }; int s = 0; for (int i = 0; i < 30; i++) s += d[i].v; return s; }
static int early(int stop) {
    for (int k = 0; k < 3; k++) {
        W a[17];
        if (k == stop) return a[16].v - a[0].v;
        if (k == 0) continue;
        W b[17];
    }
    return -1;
}

int main()
{
    lfWatch();
    printf("17: %d\n", sum17());
    printf("65: %d\n", sum65());
    const int thousand = sum1000(), plain = sumPlain();
    printf("1000: %d built %d gone %d in order %d\n", thousand, cBuilt, cGone, inOrder);
    printf("plain: %d built %d\n", plain, nBuilt);
    printf("defaults: %d\n", sumDefaults());
    serial = 0;
    printf("grid: %d\n", sumGrid());
    serial = 0;
    printf("list: %d\n", sumList());
    printf("list defaults: %d\n", sumListDefaults());
    printf("early: %d %d %d\n", early(0), early(1), early(2));
    printf("so far: built %d gone %d\n", lfBuiltCount, lfGoneCount);
    return 0;
}
