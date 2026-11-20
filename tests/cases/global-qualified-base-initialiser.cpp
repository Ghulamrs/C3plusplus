// [class.base.init]/2: `: ::X(a)` - a base named with a leading `::` in a mem-initializer, inside `n` beside an
// `n::X`, and `: ::m::Y(a)` through a namespace; each base built once and destroyed once under the ledger.
extern "C" int printf(const char *, ...);
#include "lifetime.h"

struct X { int v; X(int a); ~X(); };
X::X(int a) : v(a) { lfBuilt(this, "X"); }
X::~X() { lfGone(this, "X"); }
namespace m { struct Y { int y; Y(int a); ~Y(); }; }
m::Y::Y(int a) : y(a * 2) { lfBuilt(this, "Y"); }
m::Y::~Y() { lfGone(this, "Y"); }
namespace n {
struct X { int w; X(int a); };
X::X(int a) : w(a + 100) {}
struct D : ::X, m::Y { D(int a); ~D(); };
D::D(int a) : ::X(a + 1), ::m::Y(a) { lfBuilt(this, "D"); }
D::~D() { lfGone(this, "D"); }
}
int main() {
    lfWatch();
    n::D d(1);
    n::X near(5);
    printf("%d %d %d\n", d.v, d.y, near.w);
    return 0;
}
