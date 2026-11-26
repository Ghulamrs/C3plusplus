// Slice 3x: an unnamed member enumeration over a parameter beside the member functions that read it, and a base of the
// class's own template over a pack (`Tuple<Rest...>`), made from the read under -templates=subst.
extern "C" int printf(const char *, ...);

template <int K, class T> struct Scale {
    enum { twice = K * 2, next, bytes = K + 1 };
    T v;
    int get() const { return twice + next + bytes; }
    T times() const { return static_cast<T>(v * twice); }
};

template <class... Ts> struct List;
template <> struct List<> { int size() const { return 0; } };
template <class T, class... Rest> struct List<T, Rest...> : List<Rest...> {
    T head;
    int size() const { return 1 + List<Rest...>::size(); }
};

int main() {
    Scale<3, short> a;
    a.v = 5;
    Scale<10, double> b;
    b.v = 0.25;
    printf("%d %d %d %.1f\n", a.get(), (int)a.times(), b.get(), b.times());
    List<int, char, double> l;
    l.head = 7;
    List<char, double> &tail = l;
    tail.head = 'q';
    printf("%d %d %c %d\n", l.size(), l.head, tail.head, (int)sizeof(List<char>));
    return 0;
}
