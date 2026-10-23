// A class returned by value is built where the caller asked - in its storage, through the hidden pointer -
// so an object holding its own address survives the trip. cxx1 built it in the callee's frame and moved the
// bytes, which C++ has no word for: a returned std::map named its sentinel in a dead frame. Found when
// cxx3 compiled by itself crashed in its first map copy. [class.copy]/31-32: `return T(...)` and
// `return f()` build the caller's object in place, a named local every `return` names is that object
// (NRVO, as clang decides it), and anything else is moved into it - the constructor runs with the right
// `this` every time.
#include <map>
#include <set>
#include <string>
extern "C" int printf(const char *, ...);
struct S {
    S *self;
    int v;
    explicit S(int x);
    S(const S &o);
    S(S &&o);
    ~S();
};
S::S(int x) : self(this), v(x) { printf("+%d ", v); }
S::S(const S &o) : self(this), v(o.v + 100) { printf("copy%d ", v); }
S::S(S &&o) : self(this), v(o.v + 1000) { printf("move%d ", v); }
S::~S() { printf("~%d%s ", v, self == this ? "" : "!"); }
S named(int k) { S s(k); s.v += 1; return s; }
S prvalue(int k) { return S(k); }
S viaCall(int k) { return prvalue(k * 2); }
S twoNames(bool c) { S a(1); S b(2); if (c) return a; return b; }
S earlyOut(bool c) { if (c) return S(7); S x(8); return x; }
S inTry(int k) {
    try { S t(k); return t; } catch (...) { return S(-1); }
}
S withOther(int k) { S other(k + 50); S r(k); return r; }
// A const local binds no `S &&`: [class.copy]/32's rvalue try fails, and it is copied.
S fromConst(int k) { const S c(k); S other(0); if (k != 0) return c; return other; }
struct Maker { virtual S make(int k) const { return S(k); } virtual ~Maker() {} };
std::map<std::string, int> table() { std::map<std::string, int> m; m["a"] = 1; m["b"] = 2; return m; }
std::set<int> evens() { std::set<int> s; for (int i = 0; i < 10; i += 2) s.insert(i); return s; }
int check(const S &s) { return s.self == &s; }
int main() {
    { S a = named(5); printf("| %d %d\n", a.v, check(a)); }
    { S b = prvalue(6); printf("| %d %d\n", b.v, check(b)); }
    { S c = viaCall(3); printf("| %d %d\n", c.v, check(c)); }
    { S d = twoNames(true); printf("| %d %d\n", d.v, check(d)); }
    { S e = earlyOut(false); printf("| %d %d\n", e.v, check(e)); }
    { S f = earlyOut(true); printf("| %d %d\n", f.v, check(f)); }
    { S g = inTry(9); printf("| %d %d\n", g.v, check(g)); }
    { S h = withOther(4); printf("| %d %d\n", h.v, check(h)); }
    { S j = fromConst(12); printf("| %d %d\n", j.v, check(j)); }
    { Maker m; const Maker &r = m; S i = r.make(11); printf("| %d %d\n", i.v, check(i)); }
    std::map<std::string, int> t = table();
    int sum = 0;
    for (std::map<std::string, int>::iterator it = t.begin(); it != t.end(); ++it) sum += it->second;
    std::set<int> e = evens();
    printf("map %d %d set %d %d\n", (int)t.size(), sum, (int)e.size(), *e.rbegin());
    return 0;
}
