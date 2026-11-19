// M8.2 2d: a deduced call whose argument is a call, under -templates=subst - the inner call asked and recorded first (its node
// is the lower, as the replay's door fires after its arguments), its result's type deducing and ranking the outer.
extern "C" int printf(const char *, ...);

template <class T> T twice(T a) { return a + a; }
template <class T> T inc(T a) { return a + 1; }
int sq(int a) { return a * a; }
double half(double d) { return d / 2; }

template <class T> T useNest(T a) { return twice(inc(a)); }
template <class T> T useMixed(T a) { return twice(sq(a)) + 1; }
template <class T> T useDep(T a) { return inc(half(a)); }

int main() {
    printf("%d %g %d\n", useNest(4), useNest(1.5), useMixed(3));
    printf("%d %g\n", useDep(5), useDep(3.0));
    return 0;
}
