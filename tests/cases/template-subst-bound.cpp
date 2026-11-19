// M8.2 2f: a type parameter bound to a plain struct, reached by reference or pointer - members read and written through `.`,
// `->`, `(*p).` and `p[i].` - made from the read under -templates=subst; a return or a local of the class by value replays.
extern "C" int printf(const char *, ...);

struct Pt { int x, y; };
struct Acc { int n; double sum; };

template <class T> int sumOf(const T &t) { return t.x + t.y; }
template <class T> void bump(T *p, int d) { p->x += d; (*p).y -= d; }
template <class T> int second(const T *p) { return p[1].x; }
template <class T> double mean(const T &a) { return a.n != 0 ? a.sum / a.n : 0.0; }
template <class T> void add(T &a, double v) { a.n++; a.sum += v; }
template <class T> T copyOf(const T &t) { return t; }
template <class T> int viaLocal(const T &t) { T u = t; return u.x; }

int main() {
    Pt p = { 3, 4 };
    Pt ps[2] = { { 1, 2 }, { 5, 6 } };
    Acc a = { 0, 0.0 };
    bump(&p, 2);
    add(a, 1.5);
    add(a, 2.5);
    printf("%d %d %d %d\n", sumOf(p), p.x, p.y, second(ps));
    printf("%.2f %d\n", mean(a), a.n);
    printf("%d %d\n", copyOf(p).x, viaLocal(p));
    return 0;
}
