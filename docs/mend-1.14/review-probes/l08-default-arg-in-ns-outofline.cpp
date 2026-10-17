// A default argument on an out-of-line namespace member reads the namespace's k. clang: 37 37.
extern "C" int printf(const char *, ...);
int k = 131;
namespace outer { int k = 37; int f(int x = k); int g(int); struct S { int v; }; int h(S s); }
int outer::f(int x) { return x; }
int outer::g(int x = k) { return x; }
int outer::h(S s) { return s.v + k - 37; }
int main() { outer::S s; s.v = 37; printf("%d %d %d\n", outer::f(), outer::g(), outer::h(s)); return 0; }
