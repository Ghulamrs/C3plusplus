// A class with a destructor returned from inside a handler: refused by the SSA lowering (outside, "C++11, owed";
// review R1/R2, 2026-11-13).
extern "C" int printf(const char *, ...);
int gone = 0;
struct S { S(int); S(const S &); ~S(); int v; };
S::S(int x) : v(x) {}
S::S(const S &o) : v(o.v) {}
S::~S() { gone++; }
S f(int n) {
    try { if (n) throw n; } catch (int e) { return S(e + 1); }
    return S(0);
}
int main() {
    { S s = f(4); printf("%d\n", s.v); }
    printf("%d\n", gone > 0);
    return 0;
}
