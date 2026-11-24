// A class template whose static data member's initialiser names a non-type parameter, made from the read under
// -templates=subst: each value evaluated under the class's values by the class body's own path.
extern "C" int printf(const char *, ...);

template <class T, T v> struct Constant {
    static const T value = v;
    typedef T value_type;
    typedef Constant<T, v> type;
    operator value_type() const { return v; }
};

template <int N> struct Sized {
    static const int size = N * 2 + 1;
    static const bool big = (N > 3);
    int cells[N];
    typedef const Sized *pointer;
    int count() const { return size; }
};
template <int N> const int Sized<N>::size;

template <class T, int N> struct Scaled {
    static const T factor = static_cast<T>(N + 1);
    T base;
    T get() const { return base * factor; }
};

struct Yes : Constant<bool, true> {};

int main() {
    Constant<int, 7> seven;
    Constant<char, 'q'> q;
    printf("%d %d %c %d\n", Constant<int, 7>::value, static_cast<int>(seven), Constant<char, 'q'>::value, static_cast<char>(q) == 'q');
    Sized<2> s2;
    Sized<5> s5;
    s2.cells[1] = 4;
    Sized<2>::pointer p = &s2;
    printf("%d %d %d %d %d\n", Sized<2>::size, Sized<5>::size, Sized<2>::big, Sized<5>::big, s5.count() + p->cells[1]);
    Scaled<short, 3> a;
    Scaled<unsigned, 9> b;
    a.base = 5;
    b.base = 2;
    printf("%d %u %d\n", a.get(), b.get(), Scaled<short, 3>::factor);
    printf("%d %d\n", Yes::value, Constant<bool, false>::type::value);
    return 0;
}
