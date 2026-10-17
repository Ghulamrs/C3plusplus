// ::k from inside a namespace that has its own k. clang: 1 2 1.
extern "C" int printf(const char *, ...);
int k = 1; typedef int T;
namespace n {
    int k = 2; typedef char T;
    int f() { return ::k; }
    int g() { return k; }
    int h() { ::T t = 1; return (int)sizeof(t); }
}
int main() { printf("%d %d %d\n", n::f(), n::g(), n::h()); return 0; }
