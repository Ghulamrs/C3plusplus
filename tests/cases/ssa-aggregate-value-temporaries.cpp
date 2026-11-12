// An aggregate with a destructor whose members' values leave temporaries that
// outlive the list ([class.temporary]/3): each member's guard stays under them
// until the whole is made, and a later value's throw destroys the members built
// ([except.ctor]/2). A range-based for over an array of a class bound by
// reference, and over a conditional of two class ranges ([expr.cond]/6); an
// array member of more elements than are built or destroyed one by one.
#define LF_BALANCE_ONLY
extern "C" int printf(const char *, ...);
#include "lifetime.h"

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

struct Built { Text symbol; int sig; bool a; bool b; };
struct Targets { Text brk; Text cont; };
struct Edit { Text name; Text value; bool undef; };

int measure(const Text &t, int k) { if (k < 0) throw k; return t.size() + k; }
Text label(const char *kind, int id) { if (id < 0) throw id; return Text(kind); }
int useB(const Built &b) { return b.symbol.size() * 100 + b.sig; }
int useT(const Targets &t) { return t.brk.size() * 10 + t.cont.size(); }
int useE(const Edit &e) { return e.name.size() * 10 + e.value.size() + (e.undef ? 100 : 0); }

struct Bag {
    int v[3];
    int n;
    Bag(int k);
    Bag(const Bag &o);
    ~Bag();
    const int *begin() const { return v; }
    const int *end() const { return v + n; }
};
Bag::Bag(int k) : n(k) { for (int i = 0; i < 3; i++) v[i] = k * 10 + i; lfBuilt(this, "Bag"); }
Bag::Bag(const Bag &o) : n(o.n) { for (int i = 0; i < 3; i++) v[i] = o.v[i]; lfBuilt(this, "Bag"); }
Bag::~Bag() { lfGone(this, "Bag"); }
Bag makeBag(int k) { return Bag(k); }

int sum(bool own, const Bag &mine) {
    int s = 0;
    for (int x : own ? mine : Bag(2)) s += x;
    for (int x : own ? makeBag(1) : makeBag(3)) s += x;
    return s;
}

static int cellsToBuild = 1000;
struct Cell {
    int k;
    Cell();
    ~Cell();
};
Cell::Cell() : k(1) { if (cellsToBuild-- == 0) throw 77; lfBuilt(this, "Cell"); }
Cell::~Cell() { lfGone(this, "Cell"); }

struct Row {
    Cell cells[20];
    Row();
};
Row::Row() {}
struct Wide { Cell cells[70]; int tail; Wide(); };
Wide::Wide() : tail(5) {}

int main() {
    lfWatch();
    Text held("held");
    const Built b = { held, measure(Text("abc"), 4), false, false };
    printf("%d\n", useB(b));
    printf("%d\n", useB(Built{ Text("xy"), measure(Text("q"), 1), true, false }));
    printf("%d\n", useT({ label("end", 1), label("begin", 2) }));
    Targets t = { label("a", 1), label("bcd", 2) };
    printf("%d\n", useT(t));
    printf("%d\n", useE(Edit{ label("name", 3), label("v", 4), false }));
    try {
        printf("%d\n", useT({ label("end", 1), label("begin", -2) }));
    } catch (int e) {
        printf("caught %d\n", e);
    }
    try {
        const Built c = { held, measure(Text("abc"), -4), false, false };
        printf("%d\n", useB(c));
    } catch (int e) {
        printf("caught %d\n", e);
    }
    const Text names[3] = { "one", "three", "" };
    int total = 0;
    for (const Text &n : names) total += n.size();
    printf("%d\n", total);
    Bag bags[2] = { Bag(1), Bag(2) };
    for (Bag &g : bags) g.n = 1;
    printf("%d %d\n", bags[0].n, bags[1].n);
    printf("%d %d\n", sum(true, bags[0]), sum(false, bags[1]));
    {
        Row r;
        Wide w;
        int cells = 0;
        for (const Cell &c : r.cells) cells += c.k;
        for (const Cell &c : w.cells) cells += c.k;
        printf("%d %d\n", cells, w.tail);
    }
    cellsToBuild = 18;
    try {
        Row r;
        printf("built %d\n", r.cells[0].k);
    } catch (int e) {
        printf("caught %d\n", e);
    }
    cellsToBuild = 66;
    try {
        Wide w;
        printf("built %d\n", w.tail);
    } catch (int e) {
        printf("caught %d\n", e);
    }
    return 0;
}
