// M8.3 slice 3m: an unnamed member enumeration of a class template, read in the body's order among its static data
// members, made from the read under -templates=subst; one naming a parameter (ByValue) is still replayed, by name.
extern "C" int printf(const char *, ...);

template <class T> struct Buf {
    enum { small = 15 };
    T data[small + 1];
    int cap() const { return small; }
};
template <int K> struct ByValue {
    enum { Twice = K * 2, Next };
    int get() const { return Twice + Next; }
};
template <class T> struct Sized {
    static const int before = 3;
    enum { bytes = 12, last = before + 1 };
    char raw[bytes];
    T t;
    static const int after = last * 2;
};
template <class T> struct Two {
    enum { a = 1 };
    enum { b = a + 2, c };
    T v;
    T sum() const { return static_cast<T>(v + b + c); }
};

int main() {
    Buf<char> b;
    Buf<int> c;
    ByValue<3> v;
    ByValue<5> w;
    Sized<int> s;
    Sized<double> d;
    Two<short> t;
    t.v = 4;
    printf("%d %d %d %d\n", b.cap(), (int)sizeof(b.data), (int)sizeof(c.data), (int)Buf<int>::small);
    printf("%d %d %d\n", v.get(), w.get(), (int)ByValue<4>::Twice);
    printf("%d %d %d %d\n", (int)sizeof(s.raw), Sized<int>::after, (int)Sized<double>::last, (int)sizeof(d.raw));
    printf("%d %d %d\n", (int)Two<short>::b, t.v, t.sum());
    return 0;
}
