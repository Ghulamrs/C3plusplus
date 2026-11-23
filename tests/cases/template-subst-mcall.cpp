// M8.2 2f: a member function called on a class bound to T - `t.f()`, `p->g(x)`, a const one through `const T &`, a base's
// on its base, an overload ranked by its argument - made from the read; a virtual one and a static one replay by name.
extern "C" int printf(const char *, ...);

struct Acc { int v; Acc(); int get() const; void add(int d); int pick(int x) const; int pick(double x) const; };
Acc::Acc() : v(1) {}
int Acc::get() const { return v; }
void Acc::add(int d) { v += d; }
int Acc::pick(int x) const { return x + 10; }
int Acc::pick(double x) const { return (int)x + 20; }
struct B0 { int b; };
struct B1 { int c; B1(); int twice() const; };
B1::B1() : c(3) {}
int B1::twice() const { return c * 2; }
struct D2 : B0, B1 { int d; D2(); };
D2::D2() : d(5) {}
struct V { int w; V(); virtual int f() const; static int s(); };
V::V() : w(4) {}
int V::f() const { return w + 1; }
int V::s() { return 7; }
template <class T> struct Cell { T x; T read() const { return x; } };

template <class T> int getOf(const T &t) { return t.get(); }
template <class T> void bump(T *p, int d) { p->add(d); p->add(1); }
template <class T> int ranked(const T &t, int i, double f) { return t.pick(i) + t.pick(f); }
template <class T> int onBase(const T &t) { return t.twice() + t.d; }
template <class T> int cell(const T *p) { return p->read() + 1; }
template <class T> int virt(const T &t) { return t.f(); }
template <class T> int stat(const T &t) { return t.s() + t.w; }

int main() {
    Acc a;
    D2 d;
    V v;
    Cell<int> c;
    c.x = 6;
    bump(&a, 2);
    printf("%d %d %d\n", getOf(a), ranked(a, 1, 2.5), onBase(d));
    printf("%d %d %d\n", cell(&c), virt(v), stat(v));
    return 0;
}
