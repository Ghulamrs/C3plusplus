// A class template whose base is a class template-id over its parameters (`Q : V<I>`), made from the read under
// -templates=subst: the base's class made first, as the replay makes it reading the head, laid by the parser's own path.
extern "C" int printf(const char *, ...);

template <int N> struct Cells {
    int cells[N];
    int size() const { return N; }
};

template <int I> struct Q : Cells<I> {
    int extra;
    int total() const { return this->size() + extra; }
};

template <class T> struct Box {
    T value;
    T get() const { return value; }
};

template <class T> struct Wrapped : public Box<T> {
    T more;
    T sum() const { return this->value + more; }
};

template <class T, int N> struct Two : Box<T>, Cells<N> {
    char tag;
};

template <class T> class Hidden : private Box<T> {
public:
    void set(T v) { this->value = v; }
    T read() const { return this->get(); }
};

template <int N> struct Mark {};
template <int N> struct Marked : Mark<N> {
    int x;
};

int main() {
    Q<3> q;
    q.cells[0] = 1;
    q.extra = 4;
    printf("%d %d %d\n", q.total(), static_cast<int>(sizeof(q)), q.cells[0]);
    Wrapped<double> w;
    w.value = 1.5;
    w.more = 2.0;
    printf("%g %g %d\n", w.sum(), w.get(), static_cast<int>(sizeof(w)));
    Two<char, 2> p;
    p.value = 'a';
    p.cells[1] = 9;
    p.tag = 'z';
    printf("%c %d %c %d %d\n", p.value, p.cells[1], p.tag, p.size(), static_cast<int>(sizeof(p)));
    Hidden<int> h;
    h.set(11);
    printf("%d %d\n", h.read(), static_cast<int>(sizeof(h)));
    Marked<5> m;
    m.x = 3;
    printf("%d %d\n", m.x, static_cast<int>(sizeof(m)));
    return 0;
}
