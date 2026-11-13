// An array of a class at static storage with more elements than a compiler would build one call at a time, with no
// initialiser and from a list with a long tail ([dcl.init.aggr]/7), at namespace scope and as a function's static:
// built first to last, destroyed at exit last first ([basic.start.term]/1).
#include "lifetime.h"

// Built first, so destroyed last: the ledger read after every array below is gone.
struct Report { Report(); ~Report(); };
Report::Report() {}
Report::~Report() { lfReport(); }
static Report report;

struct W { int v; W(); W(int x); ~W(); };
static int serial = 0;
W::W() : v(serial++) { lfBuilt(this, "W"); }
W::W(int x) : v(1000 + x) { lfBuilt(this, "W"); }
W::~W() { lfGone(this, "W"); }

struct N { int v; N(); N(int x); };
static int nBuilt = 0;
N::N() : v(nBuilt++) {}
N::N(int x) : v(x) { nBuilt++; }

static W plain[17];
static W listed[65] = { 1, 2, 3 };
W shown[70] = { 9 };
static N bare[1000];
static N bareListed[100] = { 5, 6 };

static int inner() {
    static W s[65] = { 4, 5 };
    static W t[17];
    return s[0].v + s[1].v + s[2].v + s[64].v + t[0].v + t[16].v;
}

static int sum(const W *w, int n) { int s = 0; for (int i = 0; i < n; i++) s += w[i].v; return s; }

int main()
{
    printf("plain: %d\n", sum(plain, 17));
    printf("listed: %d %d %d %d %d\n", listed[0].v, listed[1].v, listed[2].v, listed[3].v, listed[64].v);
    printf("shown: %d %d %d\n", shown[0].v, shown[1].v, shown[69].v);
    printf("bare: %d %d built %d\n", bare[0].v + bare[999].v, bareListed[0].v + bareListed[1].v + bareListed[2].v + bareListed[99].v, nBuilt);
    printf("inner: %d %d\n", inner(), inner());
    printf("built: %d\n", lfBuiltCount);
    return 0;
}
