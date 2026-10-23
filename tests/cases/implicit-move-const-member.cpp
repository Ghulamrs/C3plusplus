// [class.copy]/15: the implicit move constructor initialises each member from an xvalue of it, and for a
// const member that is `const K &&`, which only the copy constructor takes - the move constructor was chosen
// and refused (M1: a map's element, `pair<const Key, V>`, is moved in now).
extern "C" int printf(const char *, ...);
struct K {
    int v;
    K(int x);
    K(const K &o);
    K(K &&o);
};
K::K(int x) : v(x) {}
K::K(const K &o) : v(o.v) { printf("copy "); }
K::K(K &&o) : v(o.v) { printf("move "); }
struct P {
    const K first;
    K second;
    P(int a, int b);
};
P::P(int a, int b) : first(a), second(b) {}
int main() {
    P a(1, 2);
    P b(static_cast<P &&>(a));
    printf("%d %d\n", b.first.v, b.second.v);
    return 0;
}
