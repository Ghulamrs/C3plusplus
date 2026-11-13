// A class with a destructor and no copy of its own returned from inside a handler, as a call's result and as a
// named local: refused by the SSA lowering ("returned inside a handler"; C++11, owed - review R2, 2026-11-13).
extern "C" int printf(const char *, ...);
int gone = 0;
struct S { int v; ~S(); };
S::~S() { gone++; }
S make(int x) { S s; s.v = x; return s; }
S f(int n) {
    try { if (n) throw n; } catch (int e) { return make(e + 1); }
    return make(0);
}
S g(int n) {
    try { if (n) throw n; } catch (int e) { S s; s.v = e + 2; return s; }
    return make(0);
}
int main() {
    { S a = f(4); S b = g(4); printf("%d %d\n", a.v, b.v); }
    printf("%d\n", gone >= 2);
    return 0;
}
