// M3b.2: a reference parameter bound to an lvalue of another type - a scalar converted into a temporary for a const
// or an rvalue reference, and a class's base subobject for a reference to the base, at offset 0 and away from it.
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; double w; };
struct D : A, B { int d; };

static long widen(const long &v) { return v * 3; }
static double asDouble(const double &v) { return v / 2; }
static int sink(int &&v) { return v + 1; }
static int readA(const A &x) { return x.a; }
static int readB(const B &x) { return x.b + (int)x.w; }
static void writeB(B &x, int v) { x.b = v; }

int main() {
    int i = 14;
    short s = 9;
    char c = 'A';
    printf("%ld %ld\n", widen(i), widen(s));
    printf("%.1f %.1f\n", asDouble(i), asDouble(c));
    printf("%d %d\n", sink(s), i);
    D d;
    d.a = 1; d.b = 2; d.w = 3.5; d.d = 4;
    printf("%d %d\n", readA(d), readB(d));
    writeB(d, 20);
    const D &cd = d;
    printf("%d %d %d\n", d.b, readB(cd), readA(cd));
    return 0;
}
