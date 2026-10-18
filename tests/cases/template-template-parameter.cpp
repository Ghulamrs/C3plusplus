// [temp.param]/1 and [temp.arg.template]: a template parameter that stands for a
// template. `C<T>` in the body is whatever template the use named, applied - a
// member, a base, nested as `C<C<T>>`, behind `typename C<T>::type`, passed on
// to another template, defaulted, and two of them in one list. The review's
// example is the first line of main. .expected is clang's output.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };
template <class T> struct Cell { T v; typedef T type; };
template <template <class> class C, class T> struct Holder { C<T> c; };
template <template <class> class C> int peek(const C<int> &x) { return x.v; }

// A base class, a nested application, a member type through the parameter.
template <template <class> class C> struct Derived : C<int> { int twice() { return this->v * 2; } };
template <template <class> class C, class T> struct Nest {
    C<C<T> > cc;
    typedef C<T> inner;
    inner one;
};
template <template <class> class C, class T> struct Typed { typename C<T>::type t; };
// Two in one list, one passed on, and a default.
template <template <class> class C, template <class> class D> struct Two { C<int> c; D<char> d; };
template <template <class> class C> struct Pass { Holder<C, int> h; };
template <template <class> class C = Box> struct Def { C<int> c; };
// One on a member function template, deduced from the argument.
struct S { template <template <class> class C> int get(const C<int> &x) const { return x.v + 1; } };

int main() {
    Holder<Box, int> h; h.c.v = 7;
    Box<int> b; b.v = 9;
    printf("%d %d\n", h.c.v, peek(b));
    Derived<Box> d; d.v = 5;
    printf("%d\n", d.twice());
    Nest<Box, int> n; n.cc.v.v = 3; n.one.v = 4;
    printf("%d %d\n", n.cc.v.v, n.one.v);
    Typed<Cell, double> t; t.t = 2.5;
    printf("%g\n", t.t);
    Two<Box, Cell> two; two.c.v = 1; two.d.v = 'x';
    printf("%d %c\n", two.c.v, two.d.v);
    Pass<Cell> p; p.h.c.v = 12;
    printf("%d\n", p.h.c.v);
    Def<> df; df.c.v = 6;
    Def<Cell> dc; dc.c.v = 8;
    printf("%d %d\n", df.c.v, dc.c.v);
    Holder<Box, Box<int> > hb; hb.c.v.v = 11;
    printf("%d\n", hb.c.v.v);
    S s;
    printf("%d\n", s.get(b));
    return 0;
}
