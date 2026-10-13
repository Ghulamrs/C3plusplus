// F27d: a range-based `for` over a braced list - [stmt.ranged]/1 binds the range to `auto &&`,
// which for braces is a `std::initializer_list`, and the loop walks it through its begin() and
// end(). Scalars, a deduced element, a const reference, a class whose elements are copied from
// named objects (no elision to differ on) and live until the loop ends, and loops nested.
#include <initializer_list>
#include "lifetime.h"
extern "C" int printf(const char *, ...);

struct P {
    int v;
    P(int x);
    P(const P &o);
    ~P();
};
P::P(int x) : v(x) { lfBuilt(this, "P"); }
P::P(const P &o) : v(o.v) { lfBuilt(this, "P"); }
P::~P() { lfGone(this, "P"); }

int main() {
    lfWatch();
    int s = 0;
    for (int x : {1, 2, 3}) s += x;
    printf("ints %d\n", s);
    for (auto d : {1.5, 2.5}) printf("%.1f ", d);
    printf("\n");
    for (const int &r : {7, 8}) s += r;
    printf("refs %d\n", s);
    int t = 0;
    P a(4), b(5), c(6);
    for (const P &p : {a, b, c}) t += p.v;
    printf("class %d\n", t);
    int grid = 0;
    for (int i : {1, 2})
        for (int j : {10, 20, 30}) grid += i * j;
    printf("nested %d\n", grid);
    for (char c : {'o', 'k'}) printf("%c", c);
    printf("\n");
    return 0;
}
