// What decltype answers, and C's value categories answered wrong (found by the expression-SFINAE
// probes, 1.15): [dcl.type.simple]/4 gives `T &` for an lvalue and `T` for a prvalue. An assignment
// and a prefix `++` are lvalues ([expr.ass]/1, [expr.pre.incr]/1); a cast to a non-reference type,
// `T(x)`, a class temporary and unary `+` are prvalues. An unparenthesised member access is the
// member's declared type whatever its object. And a reference binds to an assignment.
template <class A, class B> struct same { static const int v = 0; };
template <class A> struct same<A, A> { static const int v = 1; };
struct P { int a; double v; P(int x); };
P::P(int x) : a(x), v(2.5) {}
int &assignRef(int &a, int b) { return a = b; }
int &bumpRef(int &a) { return ++a; }
extern "C" int printf(const char *, ...);
int main() {
    int t = 1, u = 2;
    const int c1 = same<decltype((int)t), int>::v;
    const int c2 = same<decltype(static_cast<int>(t)), int>::v;
    const int c3 = same<decltype(int(t)), int>::v;
    const int c4 = same<decltype(+t), int>::v;
    const int c5 = same<decltype(P(1)), P>::v;
    printf("prvalues %d %d %d %d %d\n", c1, c2, c3, c4, c5);
    const int l1 = same<decltype(t = u), int &>::v;
    const int l2 = same<decltype(++t), int &>::v;
    const int l3 = same<decltype(t += 2), int &>::v;
    const int l4 = same<decltype(static_cast<int &>(t)), int &>::v;
    const int l5 = same<decltype((t)), int &>::v;
    printf("lvalues %d %d %d %d %d\n", l1, l2, l3, l4, l5);
    const int m1 = same<decltype(P(1).v), double>::v;
    const int m2 = same<decltype(P(1).a), int>::v;
    printf("members %d %d\n", m1, m2);
    int x = 1, y = 5;
    assignRef(x, 7) += 1;
    int &r = (y = 9);
    r += 1;
    bumpRef(bumpRef(x));
    printf("bound %d %d\n", x, y);
    return 0;
}
