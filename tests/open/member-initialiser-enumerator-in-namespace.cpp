// **A default member initialiser naming an enumerator of an enum declared in the enclosing namespace.** The
// initialiser is replayed per constructor with the enclosing locals put aside, and the namespace goes with them, so
// `kNoCond` is "'kNoCond' was not declared" where clang finds `opt::kNoCond` by [basic.lookup.unqual]. A mem-initialiser
// `Value() : cc(kNoCond) {}` finds it, and so does the same initialiser with the enum at global scope - it is the
// replay's scope, not the enum. Found 2026-10-26 by tools/self-host: src/optimizer/OptValues.cpp:18 is this shape, and
// stage 0 at f5967f9 does not build stage 1 because of it.
#include <stdio.h>

namespace opt {
enum Cond : int { kNoCond = -1, kEq };
struct Value {
    int k = 0;
    Cond cc = kNoCond;
};
int probe() { Value v; return v.cc; }
}

int main() {
    printf("%d\n", opt::probe());
    return 0;
}
