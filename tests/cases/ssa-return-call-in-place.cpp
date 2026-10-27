// `return f();` for a class with a copy constructor of its own: the callee builds the caller's object through this
// function's own result pointer - from a free call, a member call, an operator, a call whose argument is a temporary
// with a destructor, and beside a local with a destructor that the return still destroys. Counted by the ledger.
#include "lifetime.h"

struct Box {
    int v;
    explicit Box(int x);
    Box(const Box &o);
    ~Box();
    Box operator+(const Box &o) const;
    Box twice() const;
};

Box::Box(int x) : v(x) { lfBuilt(this, "Box"); }
Box::Box(const Box &o) : v(o.v + 1000) { lfBuilt(this, "Box"); }
Box::~Box() { lfGone(this, "Box"); }
Box Box::operator+(const Box &o) const { return Box(v + o.v); }
Box Box::twice() const { return Box(v * 2); }

struct Note {
    int n;
    explicit Note(int x);
    ~Note();
};

Note::Note(int x) : n(x) { lfBuilt(this, "Note"); }
Note::~Note() { lfGone(this, "Note"); }

static int noted = 0;

Box make(int x) { return Box(x); }
Box fromNote(const Note &n) { return Box(n.n * 10); }

Box viaCall(int x) { return make(x + 1); }
Box viaMember(const Box &b) { return b.twice(); }
Box viaOperator(const Box &a, const Box &b) { return a + b; }
Box viaTemporary(int x) { return fromNote(Note(x)); }
Box besideLocal(int x) {
    Note keep(x);
    noted += keep.n;
    return make(keep.n * 3);
}
Box twoWays(int x) {
    if (x > 0) return viaCall(x);
    return (make(-x));
}

int main() {
    lfWatch();
    Box a = viaCall(4);
    printf("viaCall %d\n", a.v);
    Box b = viaMember(a);
    printf("viaMember %d\n", b.v);
    Box c = viaOperator(a, b);
    printf("viaOperator %d\n", c.v);
    Box d = viaTemporary(7);
    printf("viaTemporary %d\n", d.v);
    Box e = besideLocal(5);
    printf("besideLocal %d noted %d\n", e.v, noted);
    Box f = twoWays(2);
    Box g = twoWays(-6);
    printf("twoWays %d %d\n", f.v, g.v);
    return 0;
}
