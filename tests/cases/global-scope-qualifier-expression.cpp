// `::f()` in an expression is the global f, whatever the namespace the call is written in has of
// that name - the trap that kept this refused: a lookup that let qualifyForLookup have its say found
// `cc::f`. The arguments are read before the absolute lookup is put down (M1 L11, 2026-10-23).
extern "C" int printf(const char *, ...);
namespace cc { int f() { return 100; } int f(int n) { return n + 100; } }
int f() { return 3; }
int f(int n) { return n; }
namespace cc { int use() { return ::f() + ::f(f(4)); } }
int main() { printf("%d\n", cc::use()); return 0; }
