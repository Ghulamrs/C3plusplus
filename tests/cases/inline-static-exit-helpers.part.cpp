// The other unit of inline-static-exit-helpers: the same inline functions, and a static of its own beside them.
extern "C" int printf(const char *, ...);
struct A { int v; A(int x); ~A(); };
inline int h() { static const A &q = A(4); return q.v; }
inline int k() { static A a(5); return a.v; }
int f() { static const A &r = A(3); return r.v; }
int use() { return h() + f(); }
int use2() { return k(); }
