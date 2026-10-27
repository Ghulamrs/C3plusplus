// Constructors and destructors of a class with a vptr, as the SSA lowering takes them (M3b.2): each stores the vptr the
// parser records - after the bases, before the members, and at a destructor's start ([class.cdtor]/4). A helper that
// dispatches shows which class the object is at each point; the constructors and destructors call nothing virtual.
extern "C" { int printf(const char *, ...); }

struct A {
    int a;
    A(int v);
    virtual int who() const;
};
int name(const A *p) { return p->who(); }
A::A(int v) : a(v) { printf("A built as %d\n", name(this)); }
int A::who() const { return 1; }

struct Probe {
    int seen;
    Probe(int s);
};
Probe::Probe(int s) : seen(s) {}

// A member built after the vptr is B's sees B.
struct B : A {
    Probe probe;
    int b;
    B(int v);
    int who() const;
};
int name(const B *p) { return p->who(); }
B::B(int v) : A(v), probe(name(this)), b(v * 2) { printf("B built as %d, its member saw %d\n", name(this), probe.seen); }
int B::who() const { return 2; }

// The class's own vptr in front of a plain base.
struct Plain {
    int p;
    Plain(int v);
};
Plain::Plain(int v) : p(v) {}
struct Z : Plain {
    int z;
    Z(int v);
    virtual int who() const;
};
int name(const Z *p) { return p->who(); }
Z::Z(int v) : Plain(v), z(v + 1) { printf("Z built as %d with %d %d\n", name(this), p, z); }
int Z::who() const { return 3; }

// Destructors: each sets its own class's vptr before its body.
struct C {
    int c;
    C(int v);
    virtual ~C();
    virtual int who() const;
};
int name(const C *p) { return p->who(); }
C::C(int v) : c(v) {}
C::~C() { printf("C destroyed as %d\n", name(this)); }
int C::who() const { return 4; }

struct D : C {
    int d;
    D(int v);
    ~D();
    int who() const;
};
int name(const D *p) { return p->who(); }
D::D(int v) : C(v), d(v) {}
D::~D() { printf("D destroyed as %d\n", name(this)); }
int D::who() const { return 5; }

struct E : D {
    E(int v);
    ~E();
    int who() const;
};
int name(const E *p) { return p->who(); }
E::E(int v) : D(v) { printf("E built as %d\n", name(this)); }
E::~E() { printf("E destroyed as %d\n", name(this)); }
int E::who() const { return 6; }

int main() {
    B b(7);
    printf("b: %d %d %d %d\n", b.a, b.b, b.probe.seen, name(&b));
    Z z(10);
    printf("z: %d %d %d\n", z.p, z.z, name(&z));
    {
        E e(3);
        printf("e: %d %d %d\n", e.c, e.d, name(&e));
    }
    C *p = new D(4);
    printf("p: %d\n", name(p));
    delete p;
    return 0;
}
