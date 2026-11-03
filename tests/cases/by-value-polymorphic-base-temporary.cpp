// A derived temporary passed by value to a polymorphic base ([class.copy]/12, [over.ics.ref]): the
// base's implicit copy constructor - its move constructor being implicit too - copies the base
// subobject of `PolyD(6)`, an xvalue as the temporary is ([basic.lval]/1) - the move binds it, and the
// copy is a Poly whose f() is Poly's. Refused by cxx3 until 2026-11-03, the subobject losing the mark.
extern "C" int printf(const char *, ...);

struct Poly { int a; Poly(int x) : a(x) {} virtual int f() const { return a + 1; } };
struct PolyD : Poly { PolyD(int x) : Poly(x) {} int f() const { return a + 2; } };
static int viaVal(Poly p) { return p.f(); }

int main() {
    printf("%d\n", viaVal(PolyD(6)));
    return 0;
}
