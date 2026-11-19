// The replay asks no argument-dependent lookup at a function template's call door: the key it ranks under (templateCallKey,
// `$global:weigh` or `lib::size`) is not the bare name, so geo's ordinary functions are never candidates ([basic.lookup.argdep]/2).
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
