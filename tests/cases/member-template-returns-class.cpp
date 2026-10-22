// A member template's signature is read in its class's scope ([basic.scope.class]): `Box &set(U)` names
// the class by its injected name, and a member typedef - of a primary, a partial specialization, and
// one in a namespace.
extern "C" int printf(const char *, ...);
template <class T> struct Box {
    typedef int count_type;
    int v;
    template <class U> Box &set(U u) { v = (int)u; return *this; }
    template <class U> count_type twice(U u) const { return 2 * (int)u + v; }
};
template <class S> struct P;
template <class R, class A> struct P<R(A)> {
    int v;
    template <class U> P *set(U u) { v = (int)u; return this; }
};
namespace n {
template <class S> struct Q;
template <class R, class A> struct Q<R(A)> { int v; template <class U> Q &set(U u) { v = (int)u; return *this; } };
}
struct Plain { int v; template <class U> Plain &set(U u) { v = (int)u + 1; return *this; } };
int main() {
    Box<int> b;
    b.set(3.5).set(4.5);
    P<int(int)> p;
    p.set(4.5)->set(5.5);
    n::Q<int(int)> q;
    q.set(6.5);
    Plain pl;
    pl.set(7.5);
    printf("%d %d %d %d %d\n", b.v, p.v, q.v, pl.v, b.twice(10.0));
    return 0;
}
