// A parameter and a local hide the namespace's name of the same spelling. clang: 7 8 5.
extern "C" int printf(const char *, ...);
namespace n {
    int k = 1; enum { Z = 1 };
    int f(int k) { return k; }
    int g() { int k = 8; return k; }
    int h() { int Z = 5; return Z; }
}
int main() { printf("%d %d %d\n", n::f(7), n::g(), n::h()); return 0; }
