// [class.union]/5: an anonymous union's members are members of the class
// around it, at one offset, named without going through it - by `.`, `->`,
// the implicit `this->`, a lambda's `[this]`, a member pointer, offsetof,
// a mem-initialiser naming one of them, one member's own initialiser, the
// implicit copy, aggregate and value initialisation, a base's. Every size
// below is clang's for x86_64-linux, x86_64-windows and arm64-darwin alike,
// and the C6000's double and long long are eight bytes aligned eight too.
// Offsets are printed, measured from the object: offsetof is not a constant expression here yet.
extern "C" int printf(const char *, ...);

struct Value { int kind; union { int i; double d; }; };
struct Around {
    char c;
    union { char a; short s; };
    char e;
    int get() const;
    int viaThis() const;
    int viaLambda() const;
};
int Around::get() const { return s; }
int Around::viaThis() const { return this->a; }
int Around::viaLambda() const { return [this]() { return (int)s; }(); }
struct Inner { int x; char y; };
struct Holds { char c; union { Inner in; long long ll; }; int tail; };
struct Bits { int kind; union { int lo : 4; unsigned hi : 12; }; char after; };
struct Named {
    int k;
    union { int i; double d; };
    Named();
    Named(int x);
    Named(double x);
};
Named::Named() : k(0), i(7) {}
Named::Named(int x) : k(1), i(x) {}
Named::Named(double x) : k(2), d(x) {}
// One member of the union carries the initialiser; a constructor naming no member of it applies it.
struct WithInit { int k; union { int i = 3; double d; }; WithInit(); };
WithInit::WithInit() : k(1) {}
struct Base { int k; union { int i; double d; }; };
struct Derived : Base { int z; };
union Outer { int a; union { char b; short c; }; };
// On arm64 the union counts once: this is an HFA of two doubles, not three.
struct Hfa { union { double a; double b; }; double c; };
static double takeHfa(Hfa h) { return h.a * 10 + h.c; }
static Hfa makeHfa(double x, double y) { Hfa h; h.b = x; h.c = y; return h; }

Value g = {1, {2}};
Value g2 = {3, 4};

static_assert(sizeof(Value) == 16, "Value: int, then the union at 8");
static_assert(sizeof(Around) == 6, "Around: char, the union at 2, char at 4");
static_assert(sizeof(Holds) == 24, "Holds: char, the union at 8, int at 16");
static_assert(sizeof(Bits) == 12, "Bits: the bit-field union is 4 bytes at 4");
static_assert(sizeof(Derived) == 24, "Derived: Base's 16, then z");
static_assert(sizeof(Outer) == 4, "Outer: a union inside a named union");
static_assert(sizeof(Hfa) == 16, "Hfa");

int main() {
    Value v;
    v.kind = 1;
    v.d = 2.5;
    printf("%d %g %d\n", v.kind, v.d, (int)sizeof(Value));
    Value *p = &v;
    p->i = 9;
    printf("%d %d\n", p->i, v.i);
    v.Value::i = 4;
    printf("%d\n", v.Value::i);

    Around r;
    r.c = 1;
    r.s = 300;
    r.e = 2;
    printf("%d %d %d\n", r.get(), r.viaThis(), r.viaLambda());

    double Value::*pd = &Value::d;
    int Value::*pi = &Value::i;
    v.d = 1.25;
    printf("%g %d\n", v.*pd, (int)sizeof(v.*pi));
    Holds hh;
    printf("%d %d %d %d %d\n", (int)((char *)&v.i - (char *)&v), (int)((char *)&v.d - (char *)&v),
           (int)((char *)&r.s - (char *)&r), (int)((char *)&r.e - (char *)&r), (int)((char *)&hh.ll - (char *)&hh));
    Base bb;
    printf("%d %d %d\n", (int)sizeof(Value::d), (int)sizeof(decltype(Value::i)),
           (int)((char *)&bb.d - (char *)&bb));

    Named n0;
    Named n1(5);
    Named n2(0.5);
    printf("%d %d | %d %d | %d %g\n", n0.k, n0.i, n1.k, n1.i, n2.k, n2.d);
    WithInit w;
    printf("%d %d\n", w.k, w.i);

    Value a = {1, {2}};
    Value b = {3, 4};
    Value z = {};
    Value q = Value();
    printf("%d %d %d %d %d %d %d %d\n", a.kind, a.i, b.kind, b.i, z.kind, z.i, q.kind, q.i);
    Holds h = {1, {{2, 3}}, 4};
    Holds h2 = {5, 6, 7, 8};
    printf("%d %d %d %d | %d %d %d %d\n", h.c, h.in.x, h.in.y, h.tail, h2.c, h2.in.x, h2.in.y, h2.tail);
    printf("%d %d %d %d\n", g.kind, g.i, g2.kind, g2.i);

    Value c = v;
    Value e;
    e = v;
    printf("%d %g %d %g\n", c.kind, c.d, e.kind, e.d);

    Derived dd;
    dd.k = 1;
    dd.d = 0.75;
    dd.z = 3;
    printf("%d %g %d\n", dd.k, dd.d, dd.z);
    Outer o;
    o.c = 1000;
    printf("%d\n", o.c);

    Bits bt;
    bt.kind = 1;
    bt.lo = 5;
    bt.after = 2;
    printf("%d %d %d\n", bt.kind, bt.lo, bt.after);
    bt.hi = 4095;
    printf("%u %d\n", bt.hi, bt.after);

    Hfa hf;
    hf.b = 1.5;
    hf.c = 2;
    Hfa made = makeHfa(7, 8);
    printf("%g %g %g\n", takeHfa(hf), made.a, made.c);
    return 0;
}
