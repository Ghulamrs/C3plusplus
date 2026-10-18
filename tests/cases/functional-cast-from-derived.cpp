// [expr.type.conv] with [class.copy]/12: `B(d)` for a trivially copyable B and a derived d copies
// the base subobject, as `B e(d);` already did; the temporary path took only a B (`CVector<4>(quat)`).
extern "C" int printf(const char *, ...);
struct B { int x; B(int a) : x(a) {} };
struct D : B { int y; D(int a) : B(a), y(7) {} };
int take(const B &b) { return b.x; }
int main() { B b(3); D d(5); B c(b); B e(d); int r1 = take(B(b)); int r2 = take(B(d)); B f = d; printf("%d %d %d %d %d\n", c.x, e.x, r1, r2, f.x); }
