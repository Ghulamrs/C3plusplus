// M8.2 2f: why a class template's specialization bound to T is made from the read only as a plain struct - each template
// reaches a specialization breaking one condition (a class-typed member, two bases, a bit-field, a static member).
extern "C" int printf(const char *, ...);

struct Noisy { int v; Noisy &operator=(const Noisy &o) { v = o.v + 100; return *this; } };
int seen(Noisy n) { return n.v; }
struct B0 { int b; };
struct B1 { int c; };

template <class T> struct Holder { Noisy n; int k; };
template <class T> struct D1 : B0, B1 { int d; };
template <class T> struct Bits { int a : 3; int b : 5; };
template <class T> struct Stat { int s0; static int s; };
template <class T> int Stat<T>::s = 40;

template <class T> void copyIn(T &a, const T &b) { a.n = b.n; }
template <class T> int byValue(const T &t) { return seen(t.n) + t.k; }
template <class T> int own(const T &t) { return t.d; }
template <class T> int inBase(const T &t) { return t.b + t.c; }
template <class T> int toBase(const T &t) { const B1 &r = t; return r.c; }
template <class T> int bits(T &t) { t.b = 9; return t.a + t.b; }
template <class T> int stat(const T &t) { return t.s + t.s0; }

int main() {
    Holder<int> h1, h2;
    h1.n.v = 1; h1.k = 2; h2.n.v = 3; h2.k = 4;
    copyIn(h1, h2);
    D1<int> d;
    d.b = 5; d.c = 6; d.d = 7;
    Bits<int> bt;
    bt.a = 1;
    Stat<int> st;
    st.s0 = 2;
    printf("%d %d %d\n", h1.n.v, byValue(h1), own(d));
    printf("%d %d\n", inBase(d), toBase(d));
    printf("%d %d\n", bits(bt), stat(st));
    return 0;
}
