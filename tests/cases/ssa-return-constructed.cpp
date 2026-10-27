// A class with constructors and no copy of its own or destructor, returned by value: its local and a temporary
// the return builds, each by its bytes - 4, 8, 16 and 24 bytes, a member class with a constructor, a branch and a loop.
extern "C" int printf(const char *, ...);

struct Small { int v; Small() : v(0) {} explicit Small(int x) : v(x * 2) {} };
struct Pair { int a, b; Pair(int x, int y) : a(x), b(y) {} };
struct Wide { long long a, b; Wide(long long x) : a(x), b(x + 1) {} };
struct Tri { double x, y, z; Tri() : x(0), y(0), z(0) {} Tri(double a, double b, double c) : x(a), y(b), z(c) {} };
struct Holds { Small s; int k; Holds(int a) : s(a), k(a + 3) {} };

Small fromTemporary(int x) { return Small(x); }
Small fromParens(int x) { return (Small(x + 1)); }
Small fromLocal(int x) { Small s(x); s.v += 1; return s; }
Small defaulted() { Small s; return s; }
Pair pairOf(int x, int y) { if (x > y) return Pair(y, x); Pair p(x, y); return p; }
Wide wide(long long x) { Wide w(x); for (int i = 0; i < 3; i++) w.a += i; return w; }
Tri tri(double a) { if (a < 0) return Tri(); return Tri(a, a * 2, a * 3); }
Holds holds(int a) { Holds h(a); h.k *= 2; return h; }
Pair fromRef(const Pair &p) { return p; }

struct Maker {
    int base;
    Maker(int b) : base(b) {}
    Pair make(int x) const { return Pair(base, x); }
    Small twice() const { Small s(base); return s; }
};

int main() {
    Small a = fromTemporary(3), b = fromParens(4), c = fromLocal(5), d = defaulted();
    printf("%d %d %d %d\n", a.v, b.v, c.v, d.v);
    Pair p = pairOf(7, 2), q = pairOf(1, 9), r = fromRef(p);
    printf("%d %d %d %d %d %d\n", p.a, p.b, q.a, q.b, r.a, r.b);
    Wide w = wide(40);
    printf("%lld %lld\n", w.a, w.b);
    Tri t = tri(1.5), u = tri(-1);
    printf("%g %g %g %g\n", t.x, t.y, t.z, u.z);
    Holds h = holds(6);
    printf("%d %d\n", h.s.v, h.k);
    Maker m(11);
    Pair mp = m.make(12);
    Small ms = m.twice();
    printf("%d %d %d\n", mp.a, mp.b, ms.v);
    return 0;
}
