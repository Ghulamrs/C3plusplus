// A virtual base with a virtual base of its own: `Deep : virtual W`, `W : virtual V`, built, copied and destroyed under
// the ledger. It died until 2026-11-10: on Microsoft Deep's flag stored its own vbptr and not W's (`??_8Deep@@7BW@@@`),
// which W's constructor reads `v` through; on Itanium the implicit copy stored no vptr, which Deep has for its virtual base alone.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
struct M { int v; M(); explicit M(int x); M(const M &o); ~M(); };
M::M() : v(1) { lfBuilt(this, "M"); printf("+M "); }
M::M(int x) : v(x) { lfBuilt(this, "M"); printf("+M%d ", v); }
M::M(const M &o) : v(o.v) { lfBuilt(this, "M"); printf("+M%dc ", v); }
M::~M() { printf("-M%d ", v); lfGone(this, "M"); }
struct V { int v; V(); V(const V &o); ~V(); };
V::V() : v(7) { lfBuilt(this, "V"); printf("+V "); }
V::V(const V &o) : v(o.v) { lfBuilt(this, "V"); printf("+Vc "); }
V::~V() { printf("-V "); lfGone(this, "V"); }
struct W : virtual V { int w; W(); W(const W &o); ~W(); };
W::W() : w(v * 2) { lfBuilt(this, "W"); printf("+W "); }
W::W(const W &o) : w(o.w) { lfBuilt(this, "W"); printf("+Wc "); }
W::~W() { printf("-W "); lfGone(this, "W"); }
struct Deep : virtual W { M dm; Deep(); ~Deep(); };   // copied by the constructor the compiler writes, so not in the ledger
Deep::Deep() : dm(6) { printf("+Deep "); }
Deep::~Deep() { printf("-Deep "); }
int main() {
    lfWatch();
    Deep dp;
    printf("| %d %d %d\n", dp.v, dp.w, dp.dm.v);
    dp.v = 9;
    Deep dq(dp);
    printf("| %d %d %d\n", dq.v, dq.w, dq.dm.v);
    return 0;
}
