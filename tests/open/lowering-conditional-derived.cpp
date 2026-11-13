// A conditional whose arms are a base and a derived class object: [expr.cond]/3 converts the derived arm to the
// base; the SSA lowering refuses "two objects apart in type" (outside, "C++11, owed"; review R1, 2026-11-13).
extern "C" int printf(const char *, ...);
struct B { B(int); B(const B &); ~B(); int v; };
struct D : B { D(int); };
B::B(int x) : v(x) {}
B::B(const B &o) : v(o.v) {}
B::~B() {}
D::D(int x) : B(x * 10) {}
int pick(bool c) { B b = c ? B(1) : D(2); return b.v; }
int main() {
    printf("%d %d\n", pick(true), pick(false));
    return 0;
}
