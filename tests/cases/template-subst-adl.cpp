// M8.2 2d: a deduced call over a pointer or an enumeration under -templates=subst - the set looked up at the instantiation
// through the replay's own lookup (Overload::candidateSet: the key's, the unnamed namespace's, the arguments' namespaces').
extern "C" int printf(const char *, ...);

namespace geo {
enum Shade { light = 1, dark = 7 };
struct Pt { int x, y; };
}
template <class T> int weigh(T) { return 1; }
template <class T> int weigh(T *) { return 2; }
int weigh(geo::Shade s) { return s * 10; }
template <class T> int useWeigh(T a) { return weigh(a); }

namespace lib {
template <class T> int size(T) { return 3; }
template <class T> int size(T *) { return 4; }
template <class T> int count(T a) { return size(a); }
}

int main() {
    geo::Pt pt = { 3, 4 };
    int n = 5;
    printf("%d %d %d %d\n", useWeigh(geo::dark), useWeigh(&pt), useWeigh(&n), useWeigh(2.5));
    printf("%d %d\n", lib::count(&pt), lib::count(n));
    return 0;
}
