// A member of nested namespace b defined inside a's block with the relative qualifier `b::`. clang: 3 3.
extern "C" int printf(const char *, ...);
int k = 1;
namespace a {
    namespace b { int k = 3; int f(); int g(int); }
    int b::f() { return k; }
    int b::g(int x = k) { return x; }
}
int main() { printf("%d %d\n", a::b::f(), a::b::g()); return 0; }
