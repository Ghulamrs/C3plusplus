// Deeper nesting, and a class local to the out-of-line body. clang: 2 2 2 9.
extern "C" int printf(const char *, ...);
int k = 1;
namespace a { int k = 2; namespace b { int f(); int g(); int h(); struct L { static int m(); }; } }
int a::b::f() { return k; }
int a::b::g() { struct Loc { int get() { return k; } }; Loc l; return l.get(); }
int a::b::h() { return [](){ return k; }(); }
int a::b::L::m() { return k + 7; }
int main() { printf("%d %d %d %d\n", a::b::f(), a::b::g(), a::b::h(), a::b::L::m()); return 0; }
