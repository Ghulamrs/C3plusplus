// `using B::B;` in a class with a virtual base of its own: the inherited constructor is split into
// CI1, which builds the virtual base, and CI2, as any constructor is. 1.13 made CI1 a second name
// for CI2, so the virtual base was never built and the program segfaulted.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
struct V { int v; V() : v(9) { lfBuilt(this, "V"); } ~V() { lfGone(this, "V"); } virtual int f() const { return v; } };
struct B {
    int b;
    B(int x) : b(x) { lfBuilt(this, "B"); }
    B(const int &x, long &y) : b(x + (int)y) { y++; lfBuilt(this, "B"); }
    ~B() { lfGone(this, "B"); }
};
struct D : B, virtual V { using B::B; int g() const { return b * 100 + v; } };
struct E : D { using D::D; int e = 4; };
struct W : virtual V { int w; W(int x) : w(x) {} };
struct F : W { using W::W; };
// A member of the derived class sits inside the range a base with a virtual base reaches;
// it is still the derived class's to initialise.
struct G : D { G() : D(2) {} int e = 6; };
int main() {
    lfWatch();
    D d(5);
    long y = 2;
    D d2(3, y);
    E e(7);
    F f(6);
    G g;
    printf("%d %d %ld %d %d %d %d %d\n", d.g(), d2.g(), y, e.g(), e.e, f.w, f.v, g.e);
    V *pv = &e;
    printf("%d\n", pv->f());
    return 0;
}
