// A base named in a mem-initialiser by a typedef, at namespace scope and as a member typedef ([class.base.init]/2):
// the lowering refused both as "a mem-initialiser of the class itself or of no base" (R1, round 7).
extern "C" int printf(const char *, ...);
struct B { int v; B(int x); };
B::B(int x) : v(x) {}
typedef B BB;
struct D : B { D(); };
D::D() : BB(7) {}
struct E : B { typedef B base; int w; E(int); };
E::E(int k) : base(k + 1), w(k) {}
int main() {
    D d;
    E e(5);
    printf("%d %d %d\n", d.v, e.v, e.w);
    return 0;
}
