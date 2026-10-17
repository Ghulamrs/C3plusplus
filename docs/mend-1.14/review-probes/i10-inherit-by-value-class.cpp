// A base constructor taking a class by value: trivially copyable is allowed, non-trivial refused by name.
extern "C" int printf(const char *, ...);
struct P { int x, y; };
struct B { int s; B(P p) : s(p.x + p.y) {} B(P p, int k) : s(p.x * k) {} };
struct D : B { using B::B; };
int main() { P p = {3, 4}; D d(p); D e(p, 10); printf("%d %d\n", d.s, e.s); return 0; }
