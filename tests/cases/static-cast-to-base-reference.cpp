// [expr.static.cast]/4: a derived lvalue cast to a reference to its base names the base subobject,
// at its offset and with its const - `static_cast<B &>(*this) = o` in an operator=, refused until the
// compiler's own C6xModel.h needed it (M1 L8d, 2026-10-23).
#include <utility>
extern "C" int printf(const char *, ...);
struct B { int x; int get() const { return x; } };
struct Pad { double d; };
struct D : Pad, B { int c; D() : c(0) { d = 0; x = 0; } D &operator=(const D &o) { static_cast<B &>(*this) = o; c++; return *this; } };
static int take(B &&b) { return b.x + 100; }
int main() {
    D a, b; a.x = 7; b = a;
    const D &cr = b;
    const int g = static_cast<const B &>(cr).get();
    static_cast<B &>(a).x = 9;
    const int t = take(static_cast<B &&>(a));
    printf("%d %d %d %d %d\n", b.x, b.c, g, a.x, t);
    return 0;
}
