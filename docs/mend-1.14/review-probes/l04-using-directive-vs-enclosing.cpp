// The enclosing namespace's k beats a using-directive's. clang: 2 2 1.
extern "C" int printf(const char *, ...);
namespace a { int k = 1; typedef char T; enum { E = 1 }; }
namespace b {
    int k = 2; typedef int T; enum { E = 2 };
    namespace c {
        using namespace a;
        int f() { return k; }
        int g() { T t = 0; return (int)sizeof(t) == 4 ? 2 : 1; }
        int h() { return E; }
    }
}
namespace d { using namespace a; int f() { return k; } }
int main() { printf("%d %d %d %d\n", b::c::f(), b::c::g(), b::c::h(), d::f()); return 0; }
