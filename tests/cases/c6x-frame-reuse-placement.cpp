// A destructor called by name ends an object and leaves its storage: `a.~Cell(); new (kept) Cell(..)` builds another there, read
// through the stored pointer while a later object lives. The frame may share bytes after the compiler's own destructor calls only.
#include <cstdio>
#include <new>
#include "lifetime.h"

struct Cell {
    int v[6];
    int tag;
    Cell(int t);
    ~Cell();
    int sum() const;
};
Cell::Cell(int t) : tag(t) { lfBuilt(this, "Cell"); for (int i = 0; i < 6; i++) v[i] = t + i; }
Cell::~Cell() { lfGone(this, "Cell"); }
int Cell::sum() const { int s = tag; for (int i = 0; i < 6; i++) s += v[i]; return s; }

Cell *kept;
void keep(Cell *p) { kept = p; }
int twice(const Cell &c) { return c.sum() * 2; }

// Inlined, `a` is no local of main's: its bytes are given by liveness.
static int reuse(int n) {
    Cell a(n);
    keep(&a);
    a.~Cell();
    new (kept) Cell(n + 10);
    int r = 0;
    { Cell b(n + 20); r = b.sum(); }
    r += twice(Cell(n + 30));
    return r + kept->sum();
}

// The same through a temporary a reference keeps.
static int extended(int n) {
    const Cell &c = Cell(n);
    keep(const_cast<Cell *>(&c));
    c.~Cell();
    new (kept) Cell(n + 1);
    int r = twice(Cell(n + 2));
    r += twice(Cell(n + 3));
    return r + kept->sum() + c.sum();
}

// Temporaries of separate statements, each destroyed by the compiler: these share.
int shared(int n) {
    int r = twice(Cell(n));
    r += twice(Cell(n + 1));
    r += twice(Cell(n + 2));
    { Cell d(n + 3); r += d.sum(); }
    return r;
}

int main() {
    lfWatch();
    int a = reuse(3);
    int b = extended(5);
    std::printf("%d %d\n", a, b);
    std::printf("%d\n", shared(7));
    return 0;
}
