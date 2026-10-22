// A class in a namespace: inline member bodies (replayed at class close) and the same names. clang: 2 2 2.
extern "C" int printf(const char *, ...);
int k = 1; enum { E = 1 };
namespace n {
    int k = 2; enum { E = 2 };
    struct S { int f() const { return k; } int g() const { return E; } static int h() { return k + E - 2; } };
}
int main() { n::S s; printf("%d %d %d\n", s.f(), s.g(), n::S::h()); return 0; }
