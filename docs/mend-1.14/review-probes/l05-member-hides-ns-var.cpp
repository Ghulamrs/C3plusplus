// A data member and a class enumerator hide the namespace's. clang: 3 9 20 20.
extern "C" int printf(const char *, ...);
namespace n {
    int k = 10; enum { E = 10 }; typedef double T;
    struct S {
        int k; enum { E = 9 }; typedef short T;
        S() : k(3) {}
        int f() { return k; }
        int g() { return E; }
        int h() { T t = 20; return t; }
        int i() { return [this]() { return k + E + (int)sizeof(T); }() - 10; }
    };
    static int gk = k;
}
int main() { n::S s; printf("%d %d %d %d %d\n", s.f(), s.g(), s.h(), s.i(), n::gk); return 0; }
