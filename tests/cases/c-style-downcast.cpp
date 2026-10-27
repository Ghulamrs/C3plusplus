// [expr.cast]/4: a C-style cast that can be a static_cast is one, so a pointer or reference to a non-virtual base cast
// down to its class steps back by the base's offset - through a deeper base, from a const pointer, null kept null.
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; double w; };
struct C : A, B { int c; };
struct E { char e[3]; };
struct D : E, C { int d; };

static C *down(B *p) { return (C *)p; }
static C &downRef(B &r) { return (C &)r; }
static D *deep(B *p) { return (D *)p; }
static C *unconst(const B *p) { return (C *)p; }

int main() {
    C x;
    x.a = 1; x.b = 2; x.w = 3.0; x.c = 4;
    D y;
    y.a = 5; y.b = 6; y.c = 7; y.d = 8;
    B *none = 0;
    int r1 = down(&x)->a;
    int r2 = downRef(x).c;
    int r3 = deep(&y)->a;
    int r4 = deep(&y)->d;
    int r5 = unconst(&x)->a;
    int r6 = down(none) == 0 ? 1 : 0;
    int r7 = down(&x) == &x ? 1 : 0;
    printf("%d %d %d %d %d %d %d\n", r1, r2, r3, r4, r5, r6, r7);
    return 0;
}
