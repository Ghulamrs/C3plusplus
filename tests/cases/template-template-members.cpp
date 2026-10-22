// A template template parameter where a class template's parameters are bound
// again later: a member function template inside such a class (two binding
// layers, the class's C under the member's U), an out-of-line member and a
// static data member defined through `Holder<C, T>::`, a member typedef through
// a typedef of the specialization, and a default on a function template's
// parameter taken on the deduced path. .expected is clang's output.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };
template <class T> struct Cell { T v; };
template <template <class> class C> struct W {
    template <class U> C<U> make(U u) { C<U> c; c.v = u; return c; }
};
template <class T, template <class> class C = Box> C<T> wrap(T t) { C<T> c; c.v = t; return c; }
template <template <class> class C, class T> struct Holder {
    C<T> c;
    typedef C<T> type;
    int get();
    static int count;
};
template <template <class> class C, class T> int Holder<C, T>::get() { return c.v + 100; }
template <template <class> class C, class T> int Holder<C, T>::count = 7;
typedef Holder<Cell, int> HC;

int main() {
    W<Box> w;
    Box<int> b = w.make(3);
    Box<double> d = wrap(2.5);
    Cell<char> ch = wrap<char, Cell>('q');
    HC h; h.c.v = 5;
    HC::type t; t.v = 6;
    printf("%d %g %c %d %d %d\n", b.v, d.v, ch.v, h.get(), t.v, HC::count);
    return 0;
}
