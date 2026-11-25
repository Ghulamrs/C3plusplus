// M8.3 slice 3m: a class over a template template parameter it names as a template-id in its braces (`C<T> c;`), made from
// the read under -templates=subst: the id's template is the one the binding binds C to (Templates::templateUnder), made before
// the class as its replay makes it. Bound to a class template, one in a namespace, one of two parameters, through a pointer
// and in a member function's signature; one bound to an alias template stays replayed. .expected is clang's output.
extern "C" int printf(const char *, ...);

template <class T> struct Box { T v; };
namespace ns { template <class T> struct Cell { T v; T w; }; }
template <class A, class B> struct Pair { A a; B b; };
template <class T> using Alias = Box<T>;

template <template <class> class C, class T> struct Holder { C<T> c; int n; };
template <template <class, class> class P, class T> struct Two { P<T, char> p; };
template <template <class> class C, class T> struct Link { C<T> *at; C<T> own; int get() const; };
template <template <class> class C, class T> int Link<C, T>::get() const { return at != 0 ? (int)at->v : -1; }
template <template <class> class C> struct Ints { C<int> i; C<short> s; };

int main() {
    Holder<Box, int> h; h.c.v = 7; h.n = 2;
    Holder<ns::Cell, char> g; g.c.v = 'a'; g.c.w = 'b'; g.n = 3;
    Holder<Box, double> d; d.c.v = 1.5; d.n = 4;
    Two<Pair, int> t; t.p.a = 40; t.p.b = 'z';
    Link<Box, int> k; k.own.v = 9; k.at = &k.own;
    Link<Box, long long> e; e.at = 0;
    Ints<Box> is; is.i.v = 100; is.s.v = 20;
    Holder<Alias, int> al; al.c.v = 5; al.n = 6;
    printf("%d %d %c %c %d %g %d\n", h.c.v, h.n, g.c.v, g.c.w, g.n, d.c.v, d.n);
    printf("%d %c %d %d %d %d\n", t.p.a, t.p.b, k.get(), e.get(), is.i.v + is.s.v, al.c.v + al.n);
    printf("%d %d %d %d\n", (int)sizeof(Holder<Box, int>), (int)sizeof(Holder<ns::Cell, char>), (int)sizeof(Two<Pair, int>), (int)sizeof(Ints<Box>));
    return 0;
}
