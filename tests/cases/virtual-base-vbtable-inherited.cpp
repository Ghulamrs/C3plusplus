// A class whose one non-virtual base holds two vbptrs holds both, and names their vbtables as that base did
// (cl: `??_8D@@7BA@@@`, `??_8D@@7BB@@@`); reached through either subobject, the virtual base is the same one.
extern "C" int printf(const char *, ...);

struct V { int v; V() : v(5) {} };
struct A : virtual V { int a; A(); };
struct B : virtual V { int b; B(); };
struct C : A, B { int c; C(); };
struct D : C { int d; D(); };
struct E : D { int e; E(); };
struct Y : virtual V { int y; Y(); };
struct F : C, Y { int f; F(); };

A::A() : a(1) {}
B::B() : b(2) {}
C::C() : c(3) {}
D::D() : d(4) {}
E::E() : e(6) {}
Y::Y() : y(7) {}
F::F() : f(8) {}

static int throughA(A *p) { return p->v; }
static int throughB(B *p) { p->v += 1; return p->v; }
static int throughY(Y *p) { p->v += 10; return p->v; }

int main() {
    D d;
    int a = throughA(&d), b = throughB(&d);
    printf("D %d %d %d\n", a, b, d.v);
    E e;
    a = throughA(&e); b = throughB(&e);
    printf("E %d %d %d %d\n", a, b, e.v, e.b + e.e);
    F f;
    b = throughB(&f);
    int y = throughY(&f);
    a = throughA(&f);
    printf("F %d %d %d %d\n", b, y, a, f.y + f.f);
    B *pb = &e;
    V *pv = pb;
    printf("same %d\n", pv == static_cast<V *>(&e) ? 1 : 0);
    return 0;
}
