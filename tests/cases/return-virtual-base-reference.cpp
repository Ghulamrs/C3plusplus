// [dcl.init.ref]/5: a reference returned to a virtual base of an lvalue binds the base subobject -
// no temporary, though reaching it takes the virtual base's step (refused as a temporary before).
#include <stdio.h>

struct V {
    int v;
    V();
};
V::V() : v(5) {}

struct W : virtual V {
    int w;
    W();
};
W::W() : w(9) {}

struct X : W {
    int x;
    X();
};
X::X() : x(11) {}

W global;

V &fromGlobal() { return global; }
V &fromParameter(W &w) { return w; }
const V &fromDeeper(const X &x) { return x; }

int main() {
    X x;
    fromGlobal().v = 6;
    fromParameter(x).v = 8;
    printf("%d %d %d\n", global.v, fromDeeper(x).v, x.w + x.x);
    printf("%d\n", &fromParameter(x) == static_cast<V *>(&x));
    return 0;
}
