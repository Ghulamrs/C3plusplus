// A declaration of a class object as the bare body of an `if` - `if (x > 0) Noisy t(60);` - is refused with
// "'Noisy' was not declared", where clang takes it ([stmt.select]/2 gives the substatement a scope of its own).
// Found writing ssa-local-destructor (M3b.2), 2026-10-27.
extern "C" int printf(const char *, ...);
static int built = 0;
struct Noisy { int v; Noisy(int x) : v(x) { built += x; } };
int main() { int x = 1; if (x > 0) Noisy t(60); printf("%d\n", built); return 0; }
