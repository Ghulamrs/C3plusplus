// <vector> owns its elements (L03): every way an element leaves - pop_back, erase of one and of
// a range, resize down, clear, assignment over it, a regrowth's old copy and the vector's own
// destructor - destroys it, and the ledger balances; the values printed are the vector's. The
// elements are copied from named objects; the ledger reports the balance, how many copies a
// regrowth makes being each library's policy. clang prints the same.
#include <vector>
#include <cstdio>
#define LF_BALANCE_ONLY  // how many copies a regrowth makes is each library's own choice
#include "lifetime.h"

struct E {
    int v;
    E();
    E(int x);
    E(const E &o);
    E &operator=(const E &o);
    ~E();
};
E::E() : v(-1) { lfBuilt(this, "E"); }
E::E(int x) : v(x) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E &E::operator=(const E &o) { v = o.v; return *this; }
E::~E() { lfGone(this, "E"); }

static void show(const char *what, const std::vector<E> &v) {
    std::printf("%s:", what);
    for (std::size_t i = 0; i < v.size(); i++) std::printf(" %d", v[i].v);
    std::printf("\n");
}

int main() {
    lfWatch();
    E a(1), b(2), c(3), d(4), z(0);
    {
        std::vector<E> v;
        for (int i = 0; i < 3; i++) { v.push_back(a); v.push_back(b); v.push_back(c); }
        show("nine", v);
        v.insert(v.begin(), d);
        v.insert(v.begin() + 5, z);
        show("insert", v);
        v.erase(v.begin() + 1);
        v.erase(v.begin() + 2, v.begin() + 5);
        show("erase", v);
        v.pop_back();
        v.resize(3);
        show("down", v);
        v.resize(5, d);
        show("up", v);
        std::vector<E> w;
        w.push_back(c);
        w = v;
        show("assign", w);
        w.clear();
        std::printf("cleared %d\n", (int)w.size());
    }
    return 0;
}
