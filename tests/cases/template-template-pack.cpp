// [temp.arg.template]/3 as clang applies it under -std=c++11 (P0522, taken as
// a defect report; g++ holds C++11's exact match): a pack parameter `template
// <class...> class C` takes Box and Tup alike, a one-parameter `template <class>
// class C` takes Tup, and a template with a parameter the list does not reach
// matches where that parameter has a default. .expected is clang's output.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box { T v; };
template <class... Ts> struct Tup { int n; };
template <class T, class U = int> struct Two { T t; U u; };
template <class T, int N = 3> struct BoxN { T v[N]; };
template <int N> struct Arr { int a[N]; };
template <int N, class T = int> struct ArrT { T a[N]; };

template <template <class...> class C> struct HP { C<int> c; };
template <template <class> class C> struct H1 { C<int> c; };
template <template <class, class> class C> struct H2 { C<int, char> c; };
template <template <int> class C> struct HN { C<3> c; };

int main() {
    HP<Box> a; a.c.v = 1;
    HP<Tup> b; b.c.n = 2;
    H1<Tup> c; c.c.n = 3;
    H1<Two> d; d.c.t = 4; d.c.u = 5;
    H1<BoxN> e; e.c.v[2] = 6;
    H2<Tup> f; f.c.n = 7;
    HN<Arr> g; g.c.a[2] = 8;
    HN<ArrT> h; h.c.a[0] = 9;
    printf("%d %d %d %d %d %d %d %d %d\n", a.c.v, b.c.n, c.c.n, d.c.t, d.c.u, e.c.v[2], f.c.n, g.c.a[2], h.c.a[0]);
    printf("%d %d\n", (int)sizeof(e.c), (int)sizeof(h.c));
    return 0;
}
