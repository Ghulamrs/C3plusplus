// Slice 3t's probe (-templates=subst): a template template parameter's id whose list the bound template lays otherwise -
// a pack parameter taking it, a pack's members one argument each, a default filled (one naming the parameter before it),
// a written value, an alias template naming a class template over its parameters - each made from the read.
// .expected is clang's output.
extern "C" { int printf(const char *, ...); }

template <class T> struct One { T v; };
template <class... Ts> struct Many { int n; };
template <class T, class U = T> struct Pair { T a; U b; };
template <class T, int N = 2> struct Row { T v[N]; };
template <int N, class T = char> struct Fixed { T a[N]; };

template <class T> using OneA = One<T>;
namespace q {
    template <class T, class U> struct Duo { T a; U b; };
    template <class T, class U> using DuoA = Duo<T, U>;
}

template <template <class> class C, class T> struct Use1 { C<T> c; C<int> d; };
template <template <class, class> class C> struct Use2 { C<int, double> c; };
template <template <class...> class C, class T> struct UseP { C<T> c; };
template <template <int> class C> struct UseN { C<4> c; };

int main() {
    Use1<Many, char> a; a.c.n = 1; a.d.n = 2;
    Use1<Pair, short> b; b.c.a = 3; b.c.b = 4; b.d.a = 5; b.d.b = 6;
    Use1<Row, long long> c; c.c.v[1] = 7; c.d.v[0] = 8;
    Use2<Many> d; d.c.n = 9;
    UseP<One, int> e; e.c.v = 10;
    UseP<Pair, char> f; f.c.a = 'a'; f.c.b = 'b';
    UseN<Fixed> g; g.c.a[3] = 'z';
    Use1<OneA, char> h; h.c.v = 'h'; h.d.v = 11;
    Use2<q::DuoA> i; i.c.a = 12; i.c.b = 0.5;
    printf("%c %d %d %g\n", h.c.v, h.d.v, i.c.a, i.c.b);
    printf("%d %d %d %d %d %d %d %d %d %d\n", a.c.n, a.d.n, b.c.a, b.c.b, b.d.a, b.d.b, (int)c.c.v[1], c.d.v[0], d.c.n, e.c.v);
    printf("%c %c %c\n", f.c.a, f.c.b, g.c.a[3]);
    printf("%d %d %d %d %d %d\n", (int)sizeof(b.c), (int)sizeof(b.d), (int)sizeof(c.c), (int)sizeof(c.d), (int)sizeof(g.c), (int)sizeof(f.c));
    return 0;
}
