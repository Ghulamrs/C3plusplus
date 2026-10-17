// D inherits B's constructors and also has a virtual base V of its own. clang: 5 9 / 5 9 / 5 9.
extern "C" int printf(const char *, ...);
struct V { int v; V() : v(9) {} virtual ~V() {} };
struct B { int a; B(int x) : a(x) {} };
struct D : B, virtual V { using B::B; };
struct E : D { E() : D(5) {} };
int main() { D d(5); E e; D *p = new D(5); printf("%d %d / %d %d / %d %d\n", d.a, d.v, e.a, e.v, p->a, p->v); delete p; return 0; }
