// M3b.2: `new T(...)` of a class with constructors, and `T(...)` as a temporary a constructor builds, under -front=ssa:
// the allocation or the frame slot, then the constructor the parser chose called on it. The output is clang's.
extern "C" int printf(const char *, ...);

static int made = 0;

struct P {
    int x, y;
    P() : x(1), y(2) { made++; }
    P(int a, int b) : x(a), y(b) { made++; }
    explicit P(int a) : x(a), y(a * 10) { made++; }
    int sum() const { return x + y; }
};

struct Pair { P a; int k; Pair(int n) : a(n, n + 1), k(n) {} };   // a member built by its own constructor

struct Z { int v; };   // no constructor of its own: `new Z()` zeroes

static int twice(int n) { return n * 2; }

int main() {
    P *p = new P(3, 4);
    P *q = new P();
    P *r = new P(twice(5));
    P *s = new P;                   // no initialiser: the default constructor, nothing zeroed first
    printf("%d %d %d %d %d %d\n", p->sum(), q->x, q->y, r->y, s->x, made);
    Pair *w = new Pair(7);
    printf("%d %d %d\n", w->a.x, w->a.y, w->k);
    int t = P(5, 6).sum() + P(2).y;
    P(9, 9).sum();                  // a temporary as a statement: built, its value unread
    printf("%d %d\n", t, made);
    Z *z = new Z();
    printf("%d %d\n", z->v, made);
    delete p; delete q; delete r; delete s; delete w; delete z;
    return 0;
}
