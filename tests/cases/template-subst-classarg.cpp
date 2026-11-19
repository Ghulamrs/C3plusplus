// M8.2 2d: a deduced call over a class lvalue, under -templates=subst - its namespaces searched as the replay's, a `C &` or
// `const C &` of its own class binding it directly, no temporary; a by-value class parameter (its copy) replays by name.
extern "C" int printf(const char *, ...);

struct Box { int v; };
struct Noisy {
    int v;
    static int copies;
    explicit Noisy(int n);
    Noisy(const Noisy &o);
    ~Noisy();
};
int Noisy::copies = 0;
Noisy::Noisy(int n) : v(n) {}
Noisy::Noisy(const Noisy &o) : v(o.v) { copies++; }
Noisy::~Noisy() {}

namespace geo {
struct Pt { int x; };
template <class T> T scale(const Pt &, T t) { return t * 2; }
}

template <class T> T pick(const Box &, T t) { return t + 1; }
template <class T> T bump(Box &, T t) { return t + 2; }
template <class T> T poke(Noisy &, T t) { return t * 3; }
template <class T> T byVal(Box, T t) { return t; }

template <class T> T usePick(T t, const Box &b) { return pick(b, t); }
template <class T> T useBump(T t, Box &b) { return bump(b, t) + 1; }
template <class T> T useScale(T t, const geo::Pt &p) { return scale(p, t); }
template <class T> T useScaleQ(T t, const geo::Pt &p) { return geo::scale(p, t); }
template <class T> T useNoisy(T t, Noisy &n) { return poke(n, t); }
template <class T> T useByVal(T t, const Box &b) { return byVal(b, t); }

int main() {
    Box b = { 5 };
    geo::Pt p = { 3 };
    Noisy n(4);
    printf("%d %g\n", usePick(1, b), usePick(1.5, b));
    printf("%d %g\n", useBump(1, b), useBump(2.5, b));
    printf("%d %g\n", useScale(3, p), useScaleQ(0.5, p));
    printf("%d %g\n", useNoisy(2, n), useNoisy(0.5, n));
    printf("%d %g %d\n", useByVal(7, b), useByVal(0.25, b), Noisy::copies);
    return 0;
}
