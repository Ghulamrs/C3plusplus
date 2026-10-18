// [temp.deduct.type]/8: `TT<T>` deduces the template template parameter from the
// argument's own template and its arguments pairwise - `C<int>` and `C<T>` from
// a `Box<int>`, `C<C<int>>` through two levels, two parameters at once, a pack
// parameter from a variadic template, `Holder<C, T>` from a `Holder<Box, int>`,
// a `C<int>` from a class derived from Box<int> ([temp.deduct.call]/4.3), and
// the same with the arguments written out. .expected is clang's output.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };
template <class T> struct Cell { T v; };
template <class T, class U> struct Pair { T a; U b; };
template <class... Ts> struct Tup { int n; };
template <template <class> class C, class T> struct Holder { C<T> c; };

template <template <class> class C> int peek(const C<int> &x) { return x.v; }
template <template <class> class C, class T> T peek2(const C<T> &x) { return x.v; }
template <template <class, class> class C> int two(const C<int, char> &x) { return x.a + x.b; }
template <template <class> class C> int deep(const C<C<int> > &x) { return x.v.v; }
template <template <class> class C, class T> int both(const C<T> &x, const C<int> &y, T t) {
    return (int)x.v + y.v + (int)t;
}
template <template <class...> class C> int pk(const C<int> &x) { return x.n; }
template <template <class> class C, template <class> class D> int twoTT(const C<int> &x, const D<int> &y) {
    return x.v * 10 + y.v;
}
template <template <class> class C, class T> int held(Holder<C, T> h, C<int> c) { return (int)h.c.v + c.v; }
template <template <class> class C> int same(Holder<C, int> h, C<Holder<C, int> > c) { return h.c.v + c.v.c.v; }
struct FromBase : Box<int> { int extra; };

int main() {
    FromBase fb; fb.v = 20; fb.extra = 0;
    Box<int> b; b.v = 1;
    Box<double> d; d.v = 2.5;
    Cell<int> ci; ci.v = 3;
    Box<Box<int> > bb; bb.v.v = 4;
    Tup<int> t; t.n = 5;
    Pair<int, char> p; p.a = 6; p.b = 1;
    Holder<Box, int> h; h.c.v = 8;
    Box<Holder<Box, int> > bh; bh.v.c.v = 9;
    printf("%d %g %d %d\n", peek(b), peek2(d), two(p), deep(bb));
    printf("%d %d %d\n", both(d, b, 2.0), pk(t), twoTT(b, ci));
    printf("%d %d\n", held(h, b), same(h, bh));
    printf("%d %d %d\n", peek<Box>(b), both<Box, double>(d, b, 1.0), twoTT<Cell, Box>(ci, b));
    printf("%d %d\n", peek(fb), peek2(fb) + fb.extra);
    return 0;
}
