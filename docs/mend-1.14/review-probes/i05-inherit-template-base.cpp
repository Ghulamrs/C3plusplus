// A class template inheriting from a dependent base, and a base with a constructor template.
extern "C" int printf(const char *, ...);
template <class T> struct Base { T v; int which; Base(T x) : v(x), which(1) {} Base(T x, int) : v(x), which(2) {} };
template <class T> struct Der : Base<T> { using Base<T>::Base; };
struct CT { int v; int which; template <class U> CT(U u) : v((int)u), which(3) {} CT(int, int) : which(4) {} };
struct DCT : CT { using CT::CT; };
int main() {
    Der<int> a(5); Der<double> b(2.5, 0);
    printf("%d %d %g %d\n", a.v, a.which, b.v, b.which);
    DCT c(3, 4); printf("%d\n", c.which);
    DCT d(7.9); printf("%d %d\n", d.v, d.which);
    return 0;
}
