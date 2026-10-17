extern "C" int printf(const char *, ...);
template <class T> struct Base { T v; int which; Base(T x) : v(x), which(1) {} Base(T x, int) : v(x), which(2) {} };
template <class T> struct Der : Base<T> { using Base<T>::Base; };
int main() { Der<int> a(5); Der<double> b(2.5, 0); printf("%d %d %g %d\n", a.v, a.which, b.v, b.which); return 0; }
