// An aggregate with a destructor whose member is initialised by a temporary of
// the member's class, a class with a copy constructor of its own - built in
// the member ([dcl.init]/17, [class.copy]/31), under the object ledger: in a
// temporary, a local, a string-like member (std::string's shape), and a later
// member's constructor throwing, which must destroy the member already built
// ([except.ctor]/2); a braced list as such an argument, a bool's compound assignment.
#define LF_BALANCE_ONLY
extern "C" int printf(const char *, ...);
#include "lifetime.h"

struct Tracked {
    int v;
    Tracked(int x);
    Tracked(const Tracked &o);
    ~Tracked();
};
Tracked::Tracked(int x) : v(x) { if (x < 0) throw x; lfBuilt(this, "Tracked"); }
Tracked::Tracked(const Tracked &o) : v(o.v) { lfBuilt(this, "Tracked"); }
Tracked::~Tracked() { lfGone(this, "Tracked"); }

struct Text {
    int n;
    Text();
    Text(const char *s);
    Text(const Text &o);
    ~Text();
    int size() const { return n; }
};
Text::Text() : n(0) { lfBuilt(this, "Text"); }
Text::Text(const char *s) : n(0) { while (s[n]) n++; lfBuilt(this, "Text"); }
Text::Text(const Text &o) : n(o.n) { lfBuilt(this, "Text"); }
Text::~Text() { lfGone(this, "Text"); }

struct Agg {
    Tracked a;
    int c;
};

struct Agg2 {
    Tracked a;
    Tracked b;
};

struct Named {
    int k;
    Text s;
};

struct Targets {
    Text brk;
    Text cont;
};

int useT(const Targets &t) { return static_cast<int>(t.brk.size() * 10 + t.cont.size()); }
int useA(const Agg &g) { return g.a.v + g.c; }
int useA2(const Agg2 &g) { return g.a.v + g.b.v; }
int useN(const Named &n) { return n.k + static_cast<int>(n.s.size()); }

int main() {
    lfWatch();
    printf("%d\n", useA(Agg{Tracked(3), 4}));
    Agg l = { Tracked(5), 6 };
    printf("%d\n", l.a.v + l.c);
    try {
        printf("%d\n", useA2(Agg2{Tracked(7), Tracked(-8)}));
    } catch (int e) {
        printf("caught %d\n", e);
    }
    printf("%d\n", useN(Named{1, Text()}));
    printf("%d\n", useN(Named{2, Text("four")}));
    printf("%d\n", useT({ "ab", "cde" }));
    bool seen = false;
    seen |= useT({ "", "x" }) == 1;
    seen &= true;
    printf("%d\n", seen ? 1 : 0);
    return 0;
}
