// M8.2 2f: a plain class bound to T asked at the member a body reaches, as a specialization is (m82ag) - each template
// reaches a class with a class-typed member, two bases, a virtual function, a bit-field or a static member.
extern "C" int printf(const char *, ...);

struct Noisy { int v; Noisy &operator=(const Noisy &o) { v = o.v + 100; return *this; } };
struct B0 { int b; };
struct B1 { int c; };

struct Holder { Noisy n; int k; };
struct D1 : B0, B1 { int d; };
struct Bits { int a : 3; int b : 5; };
struct Stat { int s0; static int s; };
int Stat::s = 40;
struct Poly { int p; Poly(); virtual int f(); };
Poly::Poly() : p(8) {}
int Poly::f() { return p + 1; }

template <class T> void copyIn(T &a, const T &b) { a.n = b.n; }
template <class T> int scalarOf(const T &t) { return t.k * 3; }
template <class T> int own(const T &t) { return t.d; }
template <class T> int inBase(const T &t) { return t.b + t.c; }
template <class T> int bits(T &t) { t.b = 9; return t.a + t.b; }
template <class T> int stat(const T &t) { return t.s + t.s0; }
template <class T> int poly(const T *p) { return p->p * 2; }

int main() {
    Holder h1, h2;
    h1.n.v = 1; h1.k = 2; h2.n.v = 3; h2.k = 4;
    copyIn(h1, h2);
    D1 d;
    d.b = 5; d.c = 6; d.d = 7;
    Bits bt;
    bt.a = 1;
    Stat st;
    st.s0 = 2;
    Poly pl;
    printf("%d %d %d\n", h1.n.v, scalarOf(h1), own(d));
    printf("%d %d\n", inBase(d), bits(bt));
    printf("%d %d %d\n", stat(st), poly(&pl), pl.f());
    return 0;
}
