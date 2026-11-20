// M8.2 2f: a type parameter bound to a class template's specialization the instantiation completed, used as a plain struct -
// members read and written through `.`, `->` and `p[i].` - made from the read under -templates=subst; the rest replays by name.
extern "C" int printf(const char *, ...);

template <class T> struct Box { T a, b; };
template <class T, int N> struct Row { T v[N]; int n; };
template <class T> struct Hid { int get() const { return x; } private: int x; };
template <class T> struct Wrap { Box<T> in; };

template <class T> int sumOf(const T &t) { return t.a + t.b; }
template <class T> void bump(T *p, int d) { p->a += d; (*p).b -= d; }
template <class T> int second(const T *p) { return p[1].a; }
template <class T> int first(const T &r) { return r.v[0] + r.n; }
template <class T> int asked(const T &h) { return h.get(); }
template <class T> int inner(const T &w) { return w.in.a; }
template <class T> int viaLocal(const T &t) { T u = t; return u.a; }

int main() {
    Box<int> p = { 3, 4 };
    Box<int> ps[2] = { { 1, 2 }, { 5, 6 } };
    Box<double> q = { 1.5, 2.5 };
    Row<int, 2> r = { { 7, 8 }, 1 };
    Hid<int> h = Hid<int>();
    Wrap<int> w = { { 9, 10 } };
    bump(&p, 2);
    printf("%d %d %d %d\n", sumOf(p), p.a, p.b, second(ps));
    printf("%.2f %d %d\n", static_cast<double>(sumOf(q)), first(r), asked(h) + inner(w));
    printf("%d\n", viaLocal(p));
    return 0;
}
