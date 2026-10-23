// An explicit constructor template: chosen for direct-initialisation and braces, never by copy-initialisation.
extern "C" int printf(const char *, ...);
struct S {
    int v;
    template <class T> explicit S(T t) : v((int)t * 2) {}
    S(const S &o) : v(o.v + 1) {}
};
struct W { int v; template <class T> W(T t) : v((int)t) {} };
int take(S s) { return s.v; }
int main() {
    S a(3.5);
    S b{4};
    S c = S(5);
    W w = 6;
    printf("%d %d %d %d\n", a.v, b.v, c.v >= 10, w.v);
    return 0;
}
