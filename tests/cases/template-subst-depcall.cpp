// M8.2 2d: a dependent-name call in a template's body resolved at the instantiation under -templates=subst, over the functions
// the definition's lookup found ([temp.dep.candidate]; ADL adds none for an arithmetic argument), ranked as any call is.
extern "C" int printf(const char *, ...);

static int calls = 0;
int pick(int k) { calls++; return k + 1; }
double pick(double d) { calls += 10; return d * 2; }
long wide(long k) { return k * 3; }
int two(int a, double b) { return a + static_cast<int>(b); }
int two(double a, int b) { return static_cast<int>(a) - b; }
template <class T> T ident(T a) { return a; }

template <class T> T chosen(T a) { return pick(a); }
template <class T> long widened(T a) { return wide(a); }
template <class T> int pair(T a, int k) { return two(a, k); }
template <class T> void alone(T a) { pick(a); }
template <class T> T viaTemplate(T a) { return ident(a); }

int main() {
    printf("%d %g %d\n", chosen(4), chosen(1.5), chosen('a'));
    printf("%ld %ld\n", widened(5), widened(static_cast<short>(7)));
    printf("%d\n", pair(2.5, 1));
    alone(3);
    alone(0.25f);
    printf("%d %d\n", viaTemplate(9), calls);
    return 0;
}
