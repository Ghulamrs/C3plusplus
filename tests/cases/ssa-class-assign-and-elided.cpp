// Class values lowered to SSA under -front=ssa: a local built straight from a call's result, of a class with a destructor
// and of one with a copy of its own, a call through an operator among them; an assignment by the copy or move
// assignment the class declares, from an object, a temporary and a call's result, and chained; and the bytes of a class with
// constructors and a destructor but no operator= of its own assigned over another; an aggregate's braced temporary and a
// constructor reached by braces; an empty class's temporary and a qualified template-id's passed by value. Counted as
// live objects and values, never as constructor calls - elision is permitted.
extern "C" int printf(const char *, ...);

int live = 0, copies = 0, moves = 0;

struct D {
    int n;
    D(int v);
    ~D();
};
D::D(int v) : n(v) { live++; }
D::~D() { live--; n = -1; }

struct C {
    int n;
    C(int v);
    C(const C &o);
    ~C();
    C &operator=(const C &o);
    C &operator=(C &&o);
};
C::C(int v) : n(v) { live++; }
C::C(const C &o) : n(o.n) { live++; }
C::~C() { live--; n = -1; }
C &C::operator=(const C &o) { n = o.n + 1000; copies++; return *this; }
C &C::operator=(C &&o) { n = o.n + 2000; o.n = 0; moves++; return *this; }

D makeD(int v);
D makeD(int v) { return D(v); }
C makeC(int v);
C makeC(int v) { return C(v); }
D operator+(const D &a, const D &b);
D operator+(const D &a, const D &b) { return D(a.n + b.n); }

int elided(int k);
int elided(int k) {
    D a = makeD(k);
    D b = makeD(k + 1);
    D s = a + b;
    C c = makeC(k * 10);
    return a.n * 1000 + b.n * 100 + s.n * 10 + c.n + live;
}

int assigned(int k);
int assigned(int k) {
    C x(1), y(k);
    x = y;
    int first = x.n;
    x = C(k + 5);
    int second = x.n;
    x = makeC(k + 7);
    int third = x.n;
    return first + second * 10 + third * 100 + live;
}

int chained(int k);
int chained(int k) {
    C a(k), b(0), c(0);
    c = b = a;
    const int n = (b = a).n;
    return c.n * 100000 + b.n * 10 + n + live;
}

int bytes(int k);
int bytes(int k) {
    D d(1);
    D e(k);
    d = e;
    int first = d.n;
    d = D(k + 3);
    int second = d.n;
    d = makeD(k + 4);
    return first + second * 10 + d.n * 100 + live;
}

struct Pt { int x; int y; };
struct Q {
    int a;
    Q(int x, int y);
};
Q::Q(int x, int y) : a(x * 10 + y) {}
struct G {
    int n;
    G(int v);
    ~G();
};
G::G(int v) : n(v) {}
G::~G() { n = -1; }

int listed(int k);
int listed(int k) {
    Pt p = Pt{k, k + 1};
    Pt q = Pt{};
    Pt r = Pt{k};
    return p.x * 10000 + p.y * 1000 + (q.x + q.y) * 100 + (r.x + r.y) * 10;
}

int built(int k);
int built(int k) {
    Q b{k, 2};
    Q c = Q(k, 3);
    Q e = Q{k, 4};
    G d = G(k + 6);
    return b.a * 1000000 + c.a * 10000 + e.a * 100 + d.n * 10 + live;
}

struct Less {
    bool operator()(int a, int b) const;
};
bool Less::operator()(int a, int b) const { return a < b; }
namespace ns {
template <class T> struct Box { T v; };
}
int pick(Less l, int a, int b);
int pick(Less l, int a, int b) { return l(a, b) ? a : b; }
int unbox(ns::Box<int> b);
int unbox(ns::Box<int> b) { return b.v + 9; }
int passed(int k);
int passed(int k) { return pick(Less(), k, 7) * 100 + unbox(ns::Box<int>()); }

int main() {
    int r = elided(2);
    printf("elided %d live %d\n", r, live);
    r = assigned(3);
    printf("assigned %d copies %d moves %d live %d\n", r, copies, moves, live);
    r = chained(2);
    printf("chained %d copies %d live %d\n", r, copies, live);
    r = bytes(4);
    printf("bytes %d live %d\n", r, live);
    printf("listed %d\n", listed(3));
    r = built(5);
    printf("built %d live %d\n", r, live);
    printf("passed %d\n", passed(3));
    return 0;
}
