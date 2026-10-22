// [namespace.udecl]/15: `using B::f;` in a class brings B's overloads of f into the class's set, but
// one the class declares with the same parameters hides B's. The compiler's own three backends write
// `using Walker::visit;` (M1 L2, 2026-10-23). A data member, or a change of access, is still refused.
extern "C" int printf(const char *, ...);
struct N1 {}; struct N2 {}; struct N3 {};
struct W {
    virtual void visit(const N1 &) { printf("W1 "); }
    virtual void visit(const N2 &) { printf("W2 "); }
    virtual ~W() {}
};
struct X : W {
    using W::visit;
    void visit(const N1 &) override { printf("X1 "); }
    void both() { N1 a; N2 b; visit(a); visit(b); }
};
struct Pad { double d; virtual ~Pad() {} };
struct Plain { int k; int f(int) const { return k + 1; } int f(double) const { return k + 2; } int g() const { return k; } };
struct Y : Pad, Plain {
    using Plain::f;
    int f(int) const { return 100; }
    int h() const { return f(1) + f(1.0) + Plain::f(1); }
};
int main() {
    W alone;        // built directly, so W's and Pad's complete-object constructors are emitted by both
    Pad padAlone;
    X x;
    x.both();
    N2 n2;
    x.visit(n2);
    W &w = x;
    N1 n1;
    w.visit(n1);
    Y y;
    y.d = 0;
    y.k = 10;
    printf("| %d %d %d\n", y.h(), y.f(2.0), y.f(3));
    return 0;
}
