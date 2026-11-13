// A constructor that throws part of the way through an array of more elements than a compiler would build one call at
// a time: [except.ctor]/3, the elements built so far are destroyed, the last built first, and nothing else is - as a
// local, from a list's tail, as a member built and as a member copied; from the first element, element 9 and the last.
#include "lifetime.h"

struct W { int v; W(); W(int x); W(const W &o); ~W(); };
static int fuse = -1, lastGone = -1, inOrder = 0;
static void burn() { if (fuse == 0) { fuse = -1; throw 9; } if (fuse > 0) fuse--; }
W::W() : v(0) { burn(); lfBuilt(this, "W"); }
W::W(int x) : v(x) { burn(); lfBuilt(this, "W"); }
W::W(const W &o) : v(o.v) { burn(); lfBuilt(this, "W"); }
W::~W() { lfGone(this, "W"); }

// Its elements numbered as built, so a destruction out of order shows.
struct S { int n; S(); S(const S &o); ~S(); };
static int made = 0;
S::S() : n(made++) { burn(); lfBuilt(this, "S"); }
S::S(const S &o) : n(made++) { (void)o; burn(); lfBuilt(this, "S"); }
S::~S() { if (lastGone < 0 || n == lastGone - 1) inOrder++; lastGone = n; lfGone(this, "S"); }

struct H { W a[17]; S b[20]; H(); };
H::H() {}

static void tell(const char *what, int at, int r) {
    printf("%s %d: %d built %d gone %d in order %d\n", what, at, r, lfBuiltCount, lfGoneCount, inOrder);
    fuse = -1; lastGone = -1; inOrder = 0; made = 0;
}

static void local(int at) {
    int r = 0;
    fuse = at;
    try { S a[17]; r = 1; } catch (int e) { r = e; }
    tell("local", at, r);
}

static void many(int at) {
    int r = 0;
    fuse = at;
    try { S a[65]; S b[17]; r = 1; } catch (int e) { r = e; }
    tell("many", at, r);
}

static void listed(int at) {
    int r = 0;
    fuse = at;
    try { W a[40] = { 1, 2 }; r = a[39].v + 1; } catch (int e) { r = e; }
    tell("listed", at, r);
}

static void member(int at) {
    int r = 0;
    fuse = at;
    try { H h; r = 1; } catch (int e) { r = e; }
    tell("member", at, r);
}

static void copied(int at) {
    int r = 0;
    H h;
    fuse = at;
    made = 0;
    try { H k(h); r = 1; } catch (int e) { r = e; }
    tell("copied", at, r);
}

int main()
{
    lfWatch();
    local(0); local(9); local(16); local(17);
    many(9); many(64); many(65 + 9);
    listed(1); listed(9); listed(39);
    member(9); member(17 + 9);
    copied(9); copied(17 + 9); copied(17 + 19); copied(40);
    return 0;
}
