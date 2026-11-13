// `static_cast<int D::*>(&C::y)`: [expr.static.cast]/12's explicit form of [conv.mem]/2 - the offset grown by the
// base's, a null kept null - typed as the implicit conversion is, on both ABIs.
extern "C" int printf(const char *, ...);
struct B { int b; };
struct C { int x; int y; };
struct D : B, C { int z; };
int D::*widen(int C::*m) { return static_cast<int D::*>(m); }
int main() {
    D d;
    d.b = 1; d.x = 2; d.y = 3; d.z = 4;
    int D::*p = static_cast<int D::*>(&C::y);
    int C::*q = &C::x;
    int D::*r = static_cast<int D::*>(q);
    int C::*s = static_cast<int C::*>(&D::y);
    int C::*n = 0;
    int D::*w = widen(n);
    printf("%d %d %d %d %d\n", d.*p, d.*r, d.*s, d.*widen(&C::y), w == 0 ? 1 : 0);
    printf("%d %d\n", static_cast<int D::*>(&C::y) == &D::y ? 1 : 0, static_cast<int D::*>(&D::z) == &D::z ? 1 : 0);
    return 0;
}
