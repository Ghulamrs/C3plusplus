// [dcl.init.list]/5 with [over.match.list]: each element of an initializer_list
// is copy-list-initialised from its braced list - built in place by the
// constructor the items choose, never by an explicit one, its arguments checked
// for narrowing. cpp11 refused a braced element when the element's class had
// constructors and no initializer_list constructor of its own. The ledger is
// what says the elements are built once each and destroyed once each.
#include <initializer_list>
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct P {
    int x, y;
    P();
    P(int a, int b = 10);
    P(const P &o);
    ~P();
};
P::P() : x(0), y(0) { lfBuilt(this, "P"); }
P::P(int a, int b) : x(a), y(b) { lfBuilt(this, "P"); }
P::P(const P &o) : x(o.x), y(o.y) { lfBuilt(this, "P"); }
P::~P() { lfGone(this, "P"); }

struct Row {
    int sum;
    Row(std::initializer_list<P> l);
};
Row::Row(std::initializer_list<P> l) : sum(0) {
    for (const P *p = l.begin(); p != l.end(); ++p) sum += p->x * 100 + p->y;
}

int total(std::initializer_list<P> l) {
    int s = 0;
    for (const P *p = l.begin(); p != l.end(); ++p) s += p->x + p->y;
    return s;
}

int main() {
    lfWatch();
    Row a = {{1, 2}, {3, 4}};        // two arguments each
    Row b{{5}, {}, {6, 7}};          // a default argument, value-initialised, two again
    int t = total({{1, 1}, {2, 2}, {3}});
    printf("%d %d %d\n", a.sum, b.sum, t);
    return 0;
}
