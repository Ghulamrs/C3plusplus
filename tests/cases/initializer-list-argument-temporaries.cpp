// A braced list made in an argument - `total(std::vector<Name>{ v, Tag("24"), "1234" })` - builds its
// elements in the backing array, and the temporaries an element's initialiser made die at the end of
// the full expression, [class.temporary]/4. They were flushed inside the list and the list then refused
// as setup an expression cannot carry; the compiler's own C6xSched.cpp writes one (M1, 2026-10-23).
// Only Tag is in the ledger: each is made once whatever copies of a Name an implementation elides.
#include <vector>
#include "lifetime.h"
extern "C" int printf(const char *, ...);
struct Tag {
    int n;
    Tag(const char *s) : n(0) { while (s[n]) n++; lfBuilt(this, "Tag"); }
    Tag(const Tag &o) : n(o.n) { lfBuilt(this, "Tag"); }
    ~Tag() { lfGone(this, "Tag"); }
};
struct Name {
    int n;
    Name(const char *s) : n(0) { while (s[n]) n++; }
    Name(const Tag &t) : n(t.n + 100) {}
};
static int total(const std::vector<Name> &v) {
    int t = 0;
    for (std::size_t i = 0; i < v.size(); i++) t = t * 1000 + v[i].n;
    return t;
}
int main() {
    lfWatch();
    Name v("abc");
    const int a = total(std::vector<Name>{ v, Tag("24"), "1234" });
    const int b = total(std::vector<Name>{ Tag("x"), Tag("yz") }) + total(std::vector<Name>{});
    printf("%d %d\n", a, b);
    return 0;
}
