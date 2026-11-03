// A derived temporary passed by value to a polymorphic base ([class.copy]/12, [over.ics.ref]): the
// base's implicit copy constructor - its move constructor being implicit too - copies the base
// subobject of `PolyD(6)`. cxx1 refuses it: "argument 2 of 'Poly' is 'struct Poly &&' and this is an
// object with an address of its own". The by-reference form and an lvalue derived object both compile.
extern "C" int printf(const char *, ...);

struct Poly { int a; Poly(int x) : a(x) {} virtual int f() const { return a + 1; } };
struct PolyD : Poly { PolyD(int x) : Poly(x) {} int f() const { return a + 2; } };
static int viaVal(Poly p) { return p.f(); }

int main() {
    printf("%d\n", viaVal(PolyD(6)));
    return 0;
}
