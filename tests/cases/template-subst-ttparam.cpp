// M8.3 slice 3s: a class over a template template parameter named as an id, `C<T>`, made from the read under -templates=subst -
// the stand-in `$TT<i>` named as the bound template in a data member, a signature, a typedef and a dependent member's class (TypedO's
// too); Pass (the parameter as an argument), Holder<Alias, int> (an alias) and Derived (a base the read lays as nothing) stay replayed.
extern "C" int printf(const char *, ...);

template <class T> struct Box { T v; };
template <class T> struct Cell { T v; typedef T type; };
template <class T> using Alias = Box<T>;
template <template <class> class C> int peek(const C<int> &x) { return x.v; }     // lays `$TT0<int>` before Two's and Def's read
template <template <class> class C, class T> T peek2(const C<T> &x) { return x.v; }
template <template <class> class C, class T> struct Holder { C<T> c; };
template <template <class> class C, template <class> class D> struct Two { C<int> c; D<char> d; };
template <template <class> class C, class T> struct Typed { typename C<T>::type t; };
template <class T> struct TypedO { typename Cell<T>::type t; };
template <template <class> class C> struct Derived : C<int> { int twice() const { return this->v * 2; } };
template <template <class> class C, class T> struct Nest { C<C<T> > cc; typedef C<T> inner; inner one; };
template <template <class> class C, class T> struct Get { C<T> c; C<T> get() const { return c; } T at(const C<T> &x) const { return x.v; } };
template <template <class> class C = Cell> struct Def { C<short> c; };
template <template <class> class C> struct Pass { Holder<C, int> h; };

int main() {
    Holder<Box, int> h; h.c.v = 7;
    Holder<Cell, char> hc; hc.c.v = 'k';
    Holder<Box, Box<int> > hb; hb.c.v.v = 11;
    printf("%d %c %d\n", h.c.v, hc.c.v, hb.c.v.v);
    Two<Box, Cell> two; two.c.v = 1; two.d.v = 'x';
    Typed<Cell, double> t; t.t = 2.5;
    TypedO<float> to; to.t = 0.5f;
    printf("%d %c %g %d %g\n", two.c.v, two.d.v, t.t, (int)sizeof(Typed<Cell, char>), (double)to.t);
    Derived<Box> d; d.v = 5;
    Nest<Cell, int> n; n.cc.v.v = 3; n.one.v = 4;
    printf("%d %d %d\n", d.twice(), n.cc.v.v, n.one.v);
    Get<Box, long long> g; g.c.v = 40;
    Box<long long> b = g.get();
    Def<> df; df.c.v = 6;
    Def<Box> db; db.c.v = 8;
    printf("%d %d %d %d\n", (int)b.v, (int)g.at(b), df.c.v + db.c.v, (int)sizeof(Def<>));
    Pass<Cell> p; p.h.c.v = 12;
    Holder<Alias, int> ha; ha.c.v = 13;
    printf("%d %d %d %d\n", p.h.c.v, ha.c.v, peek(two.c), peek2(h.c));
    return 0;
}
