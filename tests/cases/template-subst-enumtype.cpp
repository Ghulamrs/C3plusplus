// Slice 3aa: a member enumeration over a type parameter beside member functions, made from the read under -templates=subst:
// its enumerators are value-dependent in the read ([temp.dep.constexpr]/2), so a body reading one replays by name.
extern "C" int printf(const char *, ...);

template <class A, class B> struct same { enum { value = 0 }; };
template <class A> struct same<A, A> { enum { value = 1 }; };

template <class T> struct Cell {
    enum { bytes = sizeof(T) * 2, isInt = same<T, int>::value, after = sizeof(T) + 1 };
    T v;
    Cell(T x);
    int size() const { return bytes + after; }
    int tagged() const { return isInt ? 100 : 200; }
    T plain() const { return v; }     // reads no enumerator
};
template <class T> Cell<T>::Cell(T x) : v(x) {}

template <class T, class U> struct Pair {
    enum Kind { First = sizeof(T), Second = sizeof(U) * 3 };
    static int total() { return First + Second; }
    Kind pick(bool f) const { return f ? First : Second; }
};

int main() {
    Cell<int> a(7);
    Cell<char> b('q');
    Cell<double> c(2.5);
    printf("%d %d %d / %d %d %d / %d %c %.1f\n", a.size(), b.size(), c.size(), a.tagged(), b.tagged(), c.tagged(), a.plain(), b.plain(),
           c.plain());
    Pair<char, short> p;
    Pair<int, double> q;
    printf("%d %d %d %d\n", Pair<char, short>::total(), Pair<int, double>::total(), (int)p.pick(false), (int)q.pick(true));
    return 0;
}
