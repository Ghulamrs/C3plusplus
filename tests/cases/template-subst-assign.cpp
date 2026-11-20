// M8.2 2f: a class-typed member of a class bound to T assigned, `a.n = b.n` - the member class's own operator=, called as
// the replay's assignValue calls it, or its bytes - made from the read.
extern "C" int printf(const char *, ...);

struct Noisy { int v; Noisy &operator=(const Noisy &o); };
Noisy &Noisy::operator=(const Noisy &o) { v = o.v + 100; return *this; }
struct Holder { Noisy n; int k; };
struct Pod { int a; };
struct Pair { Pod p; int q; };

template <class T> void copyIn(T &a, const T &b) { a.n = b.n; }
template <class T> void viaPtr(T *a, const T *b) { a->n = b->n; a->k = b->k; }
template <class T> void bytes(T &a, const T &b) { a.p = b.p; }

int main() {
    Holder h1, h2, h3;
    h1.n.v = 1;
    h1.k = 2;
    h2.n.v = 3;
    h2.k = 4;
    h3.n.v = 0;
    h3.k = 0;
    copyIn(h1, h2);
    viaPtr(&h3, &h2);
    Pair x, y;
    x.p.a = 5;
    y.p.a = 6;
    bytes(x, y);
    printf("%d %d %d %d\n", h1.n.v, h3.n.v, h3.k, x.p.a);
    return 0;
}
