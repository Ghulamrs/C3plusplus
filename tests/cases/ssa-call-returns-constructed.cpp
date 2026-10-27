// A call returning a class with constructors and no copy of its own or destructor: its result taken as a local, a
// member read, a const reference and an argument, returned on, and through an operator; such a class passed by value
// from a local, a temporary and a call; and one returned from `*this`, a member, an element and a global.
extern "C" int printf(const char *, ...);

struct Small {
    int v;
    Small() : v(0) {}
    explicit Small(int x) : v(x * 3) {}
};

struct Wide {
    long long a, b, c;
    Wide(long long x, long long y) : a(x), b(y), c(x + y) {}
    Wide operator+(const Wide &o) const { return Wide(a + o.a, b + o.b); }
};

struct It {
    int *p;
    It(int *q) : p(q) {}
    It self() const { return *this; }
};

struct Span {
    It from, to;
    Small sizes[2];
    Span(int *a, int *b) : from(a), to(b) { sizes[0] = Small(1); sizes[1] = Small(2); }
    It first() const { return from; }
    Small second() const { return sizes[1]; }
};

static Small kept(8);
static Small global() { return kept; }

struct Box {
    int data[4];
    Box() { for (int i = 0; i < 4; i++) data[i] = i * 10 + 1; }
    It begin() { return It(data); }
    It end() { return It(data + 4); }
    It first() { return begin(); }
};

static Small make(int x) { return Small(x); }
static Small again(int x) { return make(x + 1); }
static Wide wide(long long x) { return Wide(x, x * 2); }
static Wide wider(long long x) { return wide(x + 5); }
static Wide sum(const Wide &l, const Wide &r) { return l + r; }
static int read(const Small &s) { return s.v; }
static long long total(const Wide &w) { return w.a + w.b + w.c; }
static Small twice(Small s) { s.v *= 2; return s; }
static long long spread(Wide w, int k) { return w.c * k; }

static void show(const Span &sp) {
    printf("%d %d %d %d\n", *sp.first().self().p, sp.second().v, global().v, (int)(sp.to.p - sp.from.p));
}

static void spans(Box &b) {
    Span sp(b.data + 1, b.data + 3);
    show(sp);
}

int main() {
    Small s = make(4);
    printf("%d %d %d\n", s.v, again(6).v, read(make(7)));
    const Small &bound = make(9);
    printf("%d\n", bound.v);
    Wide w = wider(3);
    printf("%lld %lld %lld\n", w.a, w.b, w.c);
    printf("%lld\n", total(wide(10)));
    Wide t = sum(wide(1), wide(2));
    printf("%lld %lld %lld\n", t.a, t.b, t.c);
    Wide u = wide(4) + wide(6);
    printf("%lld\n", total(u));
    Box b;
    int n = 0;
    for (It i = b.begin(), e = b.end(); i.p != e.p; i.p++) n += *i.p;
    printf("%d %d\n", n, *b.first().p);
    int k = 0;
    for (int i = 0; i < 5; i++) k += make(i).v;
    printf("%d\n", k);
    Small p(5);
    printf("%d %d %d %d\n", twice(p).v, p.v, twice(Small(2)).v, twice(make(1)).v);
    printf("%lld %lld\n", spread(Wide(1, 2), 3), spread(wide(5), 2));
    spans(b);
    return 0;
}
