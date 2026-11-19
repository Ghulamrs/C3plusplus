// A global X and an `n::X` whose constructors take the same arguments: inside `n`, `::X a(1)`, `::X(2)`, a
// typedef of `::X`, a base `::X`, `new ::X(5)` and `::X f = 6` build the global one. cxx3 re-qualified the
// constructor's key through `n` and called `n::X::X` on a global X's storage - a wrong answer, not a refusal
// (lane m6d-glob, 2026-11-19).
extern "C" int printf(const char *, ...);
struct X { int v; X(int a); };
X::X(int a) : v(a) {}
namespace n {
struct X { int w, u, t; X(int a); };
X::X(int a) : w(a + 100), u(0), t(0) {}
struct D : ::X { D(int a); };
D::D(int a) : X(a + 1) {}
int one() { ::X a(1); return a.v; }
int two() { return ::X(2).v; }
int three() { typedef ::X G; G c(3); return c.v; }
int four() { const X mine(4); return mine.w; }
int five() { D d(4); ::X *p = new ::X(5); const int r = d.v + p->v; delete p; return r; }
int six() { ::X f = 6; return f.v + (int)sizeof(::X); }
}
int main() {
    printf("%d %d %d %d %d %d\n", n::one(), n::two(), n::three(), n::four(), n::five(), n::six());
    return 0;
}
