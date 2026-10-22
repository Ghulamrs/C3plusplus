// A nested class of a specialization used from outside: a parameter type, new, a static named only from inside. clang: 3 9 5.
extern "C" int printf(const char *, ...);
template <class T> struct Tree {
    struct It { T v; It(T x) : v(x) {} int twice() const { return (int)v * 2 + hidden; } static int hidden; };
};
template <class T> int Tree<T>::It::hidden = 5;
int read(const Tree<int>::It &it) { return (int)it.v; }
int main() {
    Tree<int>::It *p = new Tree<int>::It(3);
    printf("%d %d %d\n", read(*p), Tree<double>::It(2.0).twice(), Tree<int>::It::hidden);
    delete p;
    return 0;
}
