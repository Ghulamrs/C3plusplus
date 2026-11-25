// A class template's member initialisers naming no template parameter, made from the read under -templates=subst: each is
// read where the replay records it, by the implicit default constructor; one beside a written constructor replays.
extern "C" int printf(const char *, ...);

template <class E> struct Flow {
    E *items;
    bool dirty = true;
    int count = 3 * 4;
    int wide{7};
    void touch() { dirty = true; }
    template <class F> int apply(F f) const { return f(count); }
};

template <class T> struct Built {
    int k = 5;
    T t;
    Built();
};

template <class T> Built<T>::Built() : t(2) {}

static int twice(int x) { return 2 * x; }

int main() {
    Flow<int> a;
    Flow<char> b;
    b.dirty = false;
    printf("%d %d %d %d\n", a.dirty, a.count, a.wide, a.apply(twice));
    b.touch();
    printf("%d %d\n", b.dirty, b.count);
    Built<int> c;
    printf("%d %d\n", c.k, c.t);
    return 0;
}
