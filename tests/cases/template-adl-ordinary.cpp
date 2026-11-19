// A function template's call asks argument-dependent lookup too ([basic.lookup.argdep]/2, [temp.dep.candidate]): the
// replay's door ranks under its own key (templateCallKey, `$global:weigh` or `lib::size`), and geo's ordinary functions,
// exact, beat the specialization ([over.match.best]).
extern "C" int printf(const char *, ...);

namespace geo {
enum Shade { light = 1, dark = 7 };
struct Pt { int x, y; };
int weigh(Shade s) { return s * 10; }
int weigh(Pt *p) { return p->x + p->y; }
int size(Pt *p) { return p->x * 3; }
}
template <class T> int weigh(T) { return 1; }
template <class T> int useWeigh(T a) { return weigh(a); }

namespace lib {
template <class T> int size(T) { return 3; }
template <class T> int count(T a) { return size(a); }
}

int main() {
    geo::Pt pt = { 3, 4 };
    int n = 5;
    printf("%d %d %d %d\n", useWeigh(geo::dark), useWeigh(&pt), useWeigh(&n), useWeigh(2.5));
    printf("%d %d\n", lib::count(&pt), lib::count(n));
    return 0;
}
