// `: ::X(a)` - a base named with a leading `::` in a mem-initializer, inside `n` beside an `n::X`. cxx3 refuses
// it with "expected a member or base to initialise"; clang takes it. The mend is in ParserTopLevel.cpp's
// mem-initializer read, which lane m82g held on 2026-11-19 (lane m6d-glob).
extern "C" int printf(const char *, ...);
struct X { int v; X(int a); };
X::X(int a) : v(a) {}
namespace n {
struct X { int w; X(int a); };
X::X(int a) : w(a + 100) {}
struct D : ::X { D(int a); };
D::D(int a) : ::X(a + 1) {}
}
int main() {
    n::D d(1);
    printf("%d\n", d.v);
    return 0;
}
