// [class.base.init] with [class.copy]/12: `D(const B &b) : B(b)` copies the base through its implicit
// copy constructor, which for a trivially copyable B is the bytes - the copy `B c(b);` already made.
// cpp11 sent the entry to overload resolution, which knew only B's written constructors, so a derived
// class could not copy its base by name (a CCS sample's `CQuat(const CVector<I> &v) : CVector<I>(v)`).
extern "C" int printf(const char *, ...);
struct B { int x; B(int a) : x(a) {} B(int a, int b) : x(a + b) {} };
struct D : B { int y; D(const B &b) : B(b), y(2) {} D(const D &d, int k) : B(d), y(k) {} };
template <int N> struct VB { int v[N]; VB(int a) { for (int i = 0; i < N; i++) v[i] = a + i; } };
template <int N> struct VD : VB<N> { int w; VD(const VB<N> &b) : VB<N>(b), w(9) {} };
struct Two : B { int z; Two(const B &b) : B(b), z(b.x * 2) {} };
int main() {
    B b(5); D d(b); D e(d, 4); VB<3> vb(7); VD<3> vd(vb); Two t(B(1, 2));
    printf("%d %d %d %d %d %d %d %d\n", d.x, d.y, e.x, e.y, vd.v[0], vd.v[2], vd.w, t.x + t.z);
    return 0;
}
