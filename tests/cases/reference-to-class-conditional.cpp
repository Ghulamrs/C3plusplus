// [class.temporary]/5: `const T &r = c ? T(..) : t;` binds the conditional's prvalue, the arm taken's object, which
// lives as long as r - and only that arm's is built and destroyed. cppparse's Sema::localDecl has the shape.
#include "lifetime.h"
struct S {
    int v;
    S(int x);
    S(const S &o);
    ~S();
};
S::S(int x) : v(x) { lfBuilt(this, "S"); }
S::S(const S &o) : v(o.v) { lfBuilt(this, "S"); }
S::~S() { lfGone(this, "S"); }
struct Named { S name; Named(); };
Named::Named() : name(7) {}
int pick(bool empty, const Named &n) {
    const S &r = empty ? S(0) : n.name;
    const S &q = (!empty ? S(40) : n.name);
    return r.v + q.v;
}
int main() {
    lfWatch();
    Named n;
    printf("%d %d\n", pick(true, n), pick(false, n));
    return 0;
}
