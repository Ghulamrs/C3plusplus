// A virtual function keeps its ref-qualifier through an override: `f() &` in the
// base and `f() &` in the derived class are one slot, and a call through a base
// pointer dispatches to the override. `g() &&` beside it is a slot of its own.
// The constructors and destructors are out of line for names.sh.
extern "C" int printf(const char *, ...);
struct B {
    int n;
    B();
    virtual ~B();
    virtual int f() &;
    virtual int g() &&;
    virtual int h() const &;
};
B::B() : n(1) {}
B::~B() {}
int B::f() & { return n; }
int B::g() && { return n * 10; }
int B::h() const & { return n * 100; }
struct D : B {
    D();
    ~D();
    int f() & override;
    int g() && override;
    int h() const & override;
};
D::D() { n = 2; }
D::~D() {}
int D::f() & { return n + 1; }
int D::g() && { return n * 10 + 1; }
int D::h() const & { return n * 100 + 1; }
D makeD() { return D(); }
int main() {
    D d;
    B *pb = &d;
    B &rb = d;
    const B &cb = d;
    printf("%d %d %d\n", pb->f(), d.f(), rb.f());
    printf("%d %d %d\n", static_cast<B &&>(rb).g(), makeD().g(), static_cast<D &&>(d).g());
    printf("%d %d %d\n", cb.h(), pb->h(), makeD().h());
    return 0;
}
