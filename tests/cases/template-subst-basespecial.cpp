// A class template whose base over its parameters has a constructor, a destructor or an assignment of its own, made from the
// read under -templates=subst: its implicit special members completeClass's under the binding, calling the base's, as the replay's.
extern "C" int printf(const char *, ...);
static int made = 0, gone = 0;

template <class T> struct Counted {
    T v;
    Counted();
    Counted(T x);
    Counted(const Counted &o);
    ~Counted();
    Counted &operator=(const Counted &o) { v = o.v + 1; return *this; }
};

template <class T> Counted<T>::Counted() : v(T()) { made++; }
template <class T> Counted<T>::Counted(T x) : v(x) { made++; }
template <class T> Counted<T>::Counted(const Counted &o) : v(o.v) { made++; }
template <class T> Counted<T>::~Counted() { gone++; }

template <class T> struct Plain : Counted<T> {
    T w;
};

template <class T> struct Own : Counted<T> {
    T w;
    Own(T a, T b);
    T sum() const { return this->v + w; }
};

template <class T> Own<T>::Own(T a, T b) : Counted<T>(a), w(b) {}

template <class T> struct Dtor {
    int k;
    ~Dtor() { gone += 10; }
};

template <class T> struct WithDtor : Dtor<T> {
    T x;
};

template <class T> struct Assign {
    T a;
    Assign &operator=(const Assign &o) { a = o.a * 2; return *this; }
};

template <class T> struct Assigned : Assign<T> {
    T b;
};

int main() {
    {
        Plain<int> p;
        p.v = 3;
        p.w = 4;
        Plain<int> q(p);
        q = p;
        printf("%d %d %d %d\n", q.v, q.w, made, gone);
        Own<int> o(5, 6);
        printf("%d %d\n", o.sum(), static_cast<int>(sizeof(o)));
    }
    printf("%d %d\n", made, gone);
    {
        WithDtor<char> d;
        d.x = 'c';
        d.k = 1;
        printf("%c %d\n", d.x, d.k);
    }
    printf("%d\n", gone);
    Assigned<int> a, b;
    a.a = 7;
    a.b = 8;
    b = a;
    printf("%d %d %d\n", b.a, b.b, static_cast<int>(sizeof(b)));
    return 0;
}
