// A non-type template parameter pack - [temp.param]/15, [temp.variadic]. The
// review's own example: a pack of ints declared in a class template and in a
// function template, counted with sizeof..., expanded into a braced list, and
// read past a head parameter. The .expected is clang's.
extern "C" int printf(const char *, ...);

template <int... Ns> struct List { static const int size = sizeof...(Ns); };
template <int... Ns> int sum() {
    int a[] = { 0, Ns... };
    int s = 0;
    for (int i = 0; i < (int)sizeof...(Ns) + 1; i++) s += a[i];
    return s;
}
template <int N, int... Rest> struct First { static const int value = N; };

// A member function of the class template, defined outside it, reading the pack.
template <int... Ns> struct Counted { int count() const; int last() const; };
template <int... Ns> int Counted<Ns...>::count() const { return sizeof...(Ns); }
template <int... Ns> int Counted<Ns...>::last() const {
    int a[] = { Ns..., -1 };
    return a[sizeof...(Ns) == 0 ? 0 : sizeof...(Ns) - 1];
}

int main() {
    printf("%d %d %d\n", List<1, 2, 3>::size, sum<4, 5, 6>(), First<7, 8, 9>::value);
    printf("%d %d %d\n", List<>::size, sum<>(), First<7>::value);
    Counted<3, 1, 4> c;
    Counted<> e;
    printf("%d %d %d %d\n", c.count(), c.last(), e.count(), e.last());
    return 0;
}
