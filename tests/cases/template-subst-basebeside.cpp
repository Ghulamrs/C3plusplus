// A class template made from the read under -templates=subst whose bases are the program's own classes beside data members
// over its parameters, laid after the bases by the parser's one base layout: each held against the same class written out.
extern "C" int printf(const char *, ...);

struct E {};
struct Data { int a; short b; };
struct Tail { int i; char c; Tail(); };
Tail::Tail() : i(7), c(1) {}

template <class T> struct One : E {
    T v;
    T twice() const { return v + v; }
};
template <class T> struct Two : Data {
    T k[2];
    long long sum() const { return a + b + k[0] + k[1]; }
};
template <class T> struct Pad : Tail {
    T d;
    int get() const { return i + c + d; }
};
template <class T, class U> struct Mix : E, Data {
    T t;
    U u;
    int total() const { return a + b + u; }
};

struct OneE : E { E v; };
struct TwoD : Data { double k[2]; };
struct PadC : Tail { char d; };
struct MixEC : E, Data { E t; char u; };

#define AT(o, m) static_cast<int>(reinterpret_cast<const char *>(&o.m) - reinterpret_cast<const char *>(&o))

int main() {
    One<int> o;
    o.v = 5;
    One<E> oe;
    OneE oe2;
    Two<double> t;
    t.a = 1, t.b = 2, t.k[0] = 3.5, t.k[1] = 4;
    TwoD t2;
    Pad<char> p;
    p.d = 2;
    PadC p2;
    Mix<E, char> m;
    m.a = 10, m.b = 20, m.u = 3;
    MixEC m2;
    printf("%d %d %d\n", o.twice(), static_cast<int>(sizeof(One<E>) == sizeof(OneE)), static_cast<int>(AT(oe, v) == AT(oe2, v)));
    printf("%lld %d %d\n", t.sum(), static_cast<int>(sizeof(Two<double>) == sizeof(TwoD)), static_cast<int>(AT(t, k) == AT(t2, k)));
    printf("%d %d %d %d\n", p.get(), p.i, static_cast<int>(sizeof(Pad<char>) == sizeof(PadC)), static_cast<int>(AT(p, d) == AT(p2, d)));
    printf("%d %d %d %d\n", m.total(), static_cast<int>(sizeof(Mix<E, char>) == sizeof(MixEC)), static_cast<int>(AT(m, t) == AT(m2, t)),
           static_cast<int>(AT(m, u) == AT(m2, u)));
    return 0;
}
