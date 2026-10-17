// A class template's nested class: out-of-line everything, called only from another deferred body.
extern "C" int printf(const char *, ...);
template <class T> struct Tree {
    struct It {
        T v; int n;
        It(T x);
        ~It();
        int twice() const;
        static int count;
        struct Deep { int d; Deep() : d(5) {} int twice(int k) const { return k * 2 + d; } int thrice(int k) const; };
        int viaDeep() const { Deep dp; return dp.twice(n) + dp.thrice(1); }
    };
    It make(T x) const { return It(x); }
    int total(T x) const { return make(x).viaDeep(); }
};
template <class T> Tree<T>::It::It(T x) : v(x), n((int)x) { count++; }
template <class T> Tree<T>::It::~It() { count--; }
template <class T> int Tree<T>::It::twice() const { return n * 2; }
template <class T> int Tree<T>::It::count = 100;
template <class T> int Tree<T>::It::Deep::thrice(int k) const { return k * 3 + d; }
int main() {
    Tree<int> t;
    printf("%d\n", t.total(4));
    printf("%d %d\n", Tree<int>::It::count, Tree<int>::It::Deep().twice(2));
    { Tree<int>::It it(7); printf("%d %d %d\n", it.twice(), Tree<int>::It::count, it.viaDeep()); }
    printf("%d %d\n", Tree<int>::It::count, Tree<double>::It::count);
    return 0;
}
