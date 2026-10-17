// A static member defined outside, and a global of the same name. clang: 5 5.
extern "C" int printf(const char *, ...);
int v = 99;
namespace n { int v = 5; struct S { static int m; static int get(); }; }
int n::S::m = v;
int n::S::get() { return v; }
int main() { printf("%d %d\n", n::S::m, n::S::get()); return 0; }
