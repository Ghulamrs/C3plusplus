// M8.2 2d: a call in a template's body with no type-dependent part is bound at the definition and made from the read under
// -templates=subst; a call over a type-dependent argument or of a dependent name replays by name.
extern "C" int printf(const char *, ...);

static int calls = 0;
int twiceOf(int k) { calls++; return 2 * k; }
inline int bump(int k) { return k + 1; }
namespace ns { long half(long k) { return k / 2; } }
template <class T> T ident(T a) { return a; }

template <class T> T scaled(T a) { int k = 3; return a * twiceOf(k); }
template <class T> T bumped(T a) { return a + bump(4) + ns::half(10L); }
template <class T> void tally(T) { twiceOf(1); }
template <class T> T viaIdent(T a) { return ident(a) + twiceOf(0); }

int main() {
    printf("%d %g\n", scaled(5), scaled(0.5));
    printf("%d %u\n", bumped(1), bumped(2u));
    tally(1);
    tally('c');
    printf("%d %g\n", viaIdent(7), viaIdent(1.5));
    printf("%d\n", calls);
    return 0;
}
