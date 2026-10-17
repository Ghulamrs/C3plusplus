// A class's own member hides a base's of the same name, and a mem-initialiser
// names the one it hides with - [class.base.init]/2, the lookup finds the
// derived class's member. The layout copies the base's members into the
// derived class's list, so a walk that matched the list's entries by name met
// B's `m` first, gave it the initialiser, and then read an empty slot for D's
// own: a crash on four lines of ordinary C++. A base member is skipped by that
// walk now; its own constructor built it.
extern "C" int printf(const char *, ...);

struct B { B(); int m; };
B::B() : m(7) {}
struct D : B { explicit D(double v); double m; };
D::D(double v) : m(v) {}

struct C { C(int k); int n; };
C::C(int k) : n(k) {}
struct E : C { E(); double n; };                       // a base named beside it
E::E() : C(3), n(4.5) {}

struct F : B { F(); int k; int m; };                   // declared after another
F::F() : m(10), k(11) {}

struct G : D { G(); char m; };                         // two levels down
G::G() : D(1.25), m('x') {}

int main() {
    D d(2.5);
    E e;
    F f;
    G g;
    printf("%.2f %d\n", d.m, d.B::m);
    printf("%.2f %d\n", e.n, e.C::n);
    printf("%d %d %d\n", f.m, f.k, f.B::m);
    printf("%c %.2f %d\n", g.m, g.D::m, g.B::m);
    return 0;
}
