// [temp.local]/1: inside a class template the template's name is the injected
// class name - this specialization - and `Tree::next(n)` reaches a static of it
// the way `Tree<T>::next(n)` does. As a type it always worked here; before `::`
// in an expression every template name went to the template-call path, whose
// class arm knew `<` and `(` and not `::`, and the program was refused with a
// message about an argument list it never wrote. An iterator nested in its
// container is where this is ordinary; the own member, an out-of-line member, a
// derived class and two template parameters are the same road.
extern "C" int printf(const char *, ...);

template <class T> struct Tree {
    typedef T value_type;
    enum { Size = 3 };
    static const int K = 10;
    static int g;
    static int next(int n) { return n + 1; }
    static void bump() { g += 1; }
    int own(int n) { return Tree::next(n); }
    int f(int n);
    struct It {
        int n;
        It() : n(Tree::next(0)) {}
        It &operator++() { n = Tree::next(n); return *this; }
        int stmt() { Tree::bump(); Tree::g += 10; return Tree::g; }
        int sum() { return Tree::Size + Tree::K; }
        value_type asValue() { Tree::value_type w = Tree::value_type(n); return w; }
        struct Deep { static int twice(int n) { return Tree::next(n) * 2; } };
    };
};
template <class T> int Tree<T>::g = 0;
template <class T> int Tree<T>::f(int n) { return Tree::next(n) + 1; }

struct D : Tree<int> { int use() { return Tree::next(4); } };

template <class T, int N> struct Pair {
    static int scale(int n) { return n * N; }
    struct In { int get() { return Pair::scale(2); } };
};

int main() {
    Tree<int>::It i;
    printf("meminit %d\n", i.n);
    ++i;
    printf("it %d\n", i.n);
    printf("stmt %d\n", i.stmt());
    printf("enum %d\n", i.sum());
    Tree<int> t;
    printf("own %d\n", t.own(1));
    printf("outofline %d\n", t.f(1));
    typedef Tree<int>::It::Deep Deep;
    printf("deep %d\n", Deep::twice(2));
    D d;
    printf("derived %d\n", d.use());
    Pair<int, 2>::In p;
    printf("pair %d\n", p.get());
    Tree<double>::It j;
    ++j;
    printf("double %.1f %d\n", j.asValue(), j.stmt());
    return 0;
}
