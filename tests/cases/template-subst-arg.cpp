// M8.2 2d: a deduced call over arguments that are not named locals, under -templates=subst - a dereference, an address, an
// arithmetic expression, a literal and a member through a pointer, each typed under the binding with its value category.
extern "C" int printf(const char *, ...);

namespace ns {
template <class T> T twice(T a) { return a + a; }
template <class T> T first(const T *p) { return *p; }
template <class T, class U> T mix(T a, U b) { return a * b; }
}
template <class T> T &pick(T &a) { return a; }

struct Box { int v; };

template <class T> T viaDeref(T *p) { pick(*p) = *p + 1; return ns::twice(*p); }
template <class T> T viaAddress(T a) { return ns::first(&a) + ns::first((&a)); }
template <class T> T viaArith(T a, T b) { return ns::twice(a - b) + ns::twice(-a); }
template <class T> T viaLiteral(T a) { return ns::mix(a, 2) + ns::mix(a, 0.5); }
template <class T> T viaMember(const Box *b, T a) { return ns::mix(a, b->v); }

int main() {
    int i = 4;
    double d = 1.5;
    Box b = { 3 };
    printf("%d %g\n", viaDeref(&i), viaDeref(&d));
    printf("%d %g\n", viaAddress(7), viaAddress(0.25));
    printf("%d %g\n", viaArith(9, 2), viaArith(2.5, 0.5));
    printf("%d %g\n", viaLiteral(5), viaLiteral(3.0));
    printf("%d %g\n", viaMember(&b, 2), viaMember(&b, 1.5));
    return 0;
}
