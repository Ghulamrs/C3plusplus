// M8.3: a bound class passed to a base's reference or pointer parameter, `g(t)` with T bound to D whose B2 is at a nonzero
// offset, in a member body and a function template's: the derived-to-base adjustment under -templates=subst, as replayed.
extern "C" int printf(const char *, ...);

struct B1 { int a; };
struct B2 { int b; };
struct D : B1, B2 { int c; };
int g(B2 &x) { return x.b; }
int h(const B2 *x) { return x->b * 10; }
template <class T> struct Use {
    int viaRef(T &t) { return g(t); }
    int viaPtr(T &t) { return h(&t); }
};
template <class T> int pass(T &t) { return g(t) + 100; }

int main() {
    D d;
    d.a = 1;
    d.b = 2;
    d.c = 3;
    Use<D> u;
    printf("%d %d %d\n", u.viaRef(d), u.viaPtr(d), pass(d));
    return 0;
}
