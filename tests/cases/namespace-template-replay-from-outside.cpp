// A class template and a function template of namespace n, instantiated from main (outside n):
// their bodies name n's k, E and T. clang: 2 2 2 1 2 2.
extern "C" int printf(const char *, ...);
int k = 1; enum { E = 1 }; typedef char T;
namespace n {
    int k = 2; enum { E = 2 }; typedef int T;
    template <class U> struct S { U u; int f() const { return k; } int g() const { return E; } int h() const { return (int)sizeof(T) == 4 ? 2 : 1; } };
    template <class U> int tf(U) { return k; }
    template <class U> int tg(U) { return E; }
}
int main() {
    n::S<double> s;
    printf("%d %d %d %d %d %d\n", s.f(), s.g(), s.h(), 1, n::tf(0), n::tg(0));
    return 0;
}
