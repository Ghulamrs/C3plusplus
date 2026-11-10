// The Builder's last kinds (M3b.3, lane F). `new E[n]` whose element constructor takes default arguments: the loop the
// compiler writes evaluates them again for each element ([dcl.fct.default]/9), and destroys the elements built so far if
// one throws ([except.ctor]/3). And an array of aggregates at namespace scope whose members' classes have constructors,
// built before main member by member ([dcl.init.aggr]/2) - from a value, a temporary, a list of a constructor's arguments,
// or value-initialised past the list's end.
#include "lifetime.h"

static int calls = 0, failAt = 0;
static int next() { return ++calls * 10; }

struct E {
    int a, b;
    E(int x = 1, int y = next());
    ~E();
};
E::E(int x, int y) : a(x), b(y) {
    if (y == failAt * 10) throw y;
    lfBuilt(this, "E");
}
E::~E() { lfGone(this, "E"); }

struct V {
    int a;
    V() : a(-1) {}
    V(int x) : a(x) {}
    V(int x, int y) : a(x * 10 + y) {}
};
struct A { V v; int k; };
struct B { int k; V v; V w; };
A table[3] = { { V(1), 2 }, { 3, 4 } };
const B single = { 5, { 6, 7 } };
static B pairs[2] = { { 8 }, { 9, 1, { 2, 3 } } };

int main() {
    lfWatch();
    E *es = new E[3];
    for (int i = 0; i < 3; i++) printf("e%d %d %d\n", i, es[i].a, es[i].b);
    delete[] es;
    failAt = calls + 3;
    try {
        E *more = new E[4];
        delete[] more;
    } catch (int n) {
        printf("caught %d after %d\n", n, calls);
    }
    printf("table %d %d | %d %d | %d %d\n", table[0].v.a, table[0].k, table[1].v.a, table[1].k, table[2].v.a, table[2].k);
    printf("single %d %d %d\n", single.k, single.v.a, single.w.a);
    printf("pairs %d %d %d | %d %d %d\n", pairs[0].k, pairs[0].v.a, pairs[0].w.a, pairs[1].k, pairs[1].v.a, pairs[1].w.a);
    return 0;
}
