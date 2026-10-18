// A non-type pack deduced from a class template specialization argument -
// [temp.deduct.type]/9: `List<Ns...>` given a `List<1, 2, 3>` binds Ns to the
// three values, `Seq<T, Vs...>` binds T and then Vs, two packs bind from two
// parameters, a pack may be followed by a deducible parameter in a function
// template ([temp.param]/11), and a pointer or a reference to the class
// deduces through itself. The .expected is clang's.
extern "C" int printf(const char *, ...);

template <int... Ns> struct List { static const int size = sizeof...(Ns); };
template <class T, T... Vs> struct Seq { static const int size = sizeof...(Vs); };

template <int... Ns> int count(List<Ns...>) { return sizeof...(Ns); }
template <int... Ns> int total(List<Ns...>) { int a[] = { 0, Ns... }; int s = 0; for (int i = 0; i <= (int)sizeof...(Ns); i++) s += a[i]; return s; }
template <int... Ns> int viaPointer(List<Ns...> *) { return sizeof...(Ns) * 10; }
template <int... Ns> int viaReference(const List<Ns...> &) { return sizeof...(Ns) * 100; }
template <class T, T... Vs> int seqSum(Seq<T, Vs...>) { T a[] = { Vs..., T() }; T s = 0; for (int i = 0; i < (int)sizeof...(Vs); i++) s += a[i]; return (int)s; }
template <class T, T... Vs> int seqSize(Seq<T, Vs...>) { return sizeof...(Vs) + (int)sizeof(T); }
template <int... As, int... Bs> int dot(List<As...>, List<Bs...>) { int p[] = { (As * Bs)..., 0 }; int s = 0; for (int i = 0; i < (int)sizeof...(As); i++) s += p[i]; return s; }
template <int... Ns, class B> int packThenType(List<Ns...>, B b) { return (int)(sizeof...(Ns) * 10 + b); }
template <int... Ns, int M> int packThenValue(List<Ns...>, List<M>) { return (int)sizeof...(Ns) * 10 + M; }
template <int... Ns> int same(List<Ns...>, List<Ns...>) { return sizeof...(Ns); }

// [temp.deduct.call]/4.3: through a base of the argument's class.
template <int... Ns> struct Derived : List<Ns...> { int extra; };

int main() {
    List<1, 2, 3> three;
    List<> none;
    Seq<int, 4, 5, 6> ints;
    Seq<char, 'a'> ch;
    Derived<7, 8> d;
    d.extra = 0;
    printf("%d %d %d %d\n", count(three), total(three), count(none), total(none));
    printf("%d %d %d\n", viaPointer(&three), viaReference(three), viaReference(none));
    printf("%d %d %d %d\n", seqSum(ints), seqSize(ints), seqSum(ch), seqSize(ch));
    printf("%d %d %d\n", dot(List<1, 2, 3>(), List<4, 5, 6>()), packThenType(three, 4.5), packThenValue(three, List<9>()));
    printf("%d %d %d\n", same(three, three), viaReference(d), viaPointer(&d));
    return 0;
}
