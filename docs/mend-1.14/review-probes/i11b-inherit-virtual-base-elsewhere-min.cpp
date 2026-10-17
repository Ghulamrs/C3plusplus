// D inherits B's constructor and has a virtual base V of its own. clang: 5 9 then 5 9.
extern "C" int printf(const char *, ...);
struct V { int v; V() : v(9) {} };
struct B { int a; B(int x) : a(x) {} };
struct D : B, virtual V { using B::B; };
struct E : D { E() : D(5) {} };
int main() { D d(5); printf("%d %d\n", d.a, d.v); E e; printf("%d %d\n", e.a, e.v); return 0; }
