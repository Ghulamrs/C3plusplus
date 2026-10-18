// [dcl.init.ref]/5: a const reference to a base binds to a temporary of a derived class - `f(D(3))`,
// `f(make())`, a template deducing V<N> from a Q<4> temporary, and `std::cout << (a + b)` through a
// friend operator<< of the base. cpp11 required an lvalue; the CCS samples' nearly(q, CQuat<>(...)).
extern "C" int printf(const char *, ...);
struct B { double x; B() : x(0) {} B(double a) : x(a) {} };
struct D : B { D(double a) : B(a) {} };
static double f(const B &b) { return b.x; }
D make() { return D(4); }
int main() { B direct(2); printf("%g %g %g\n", f(D(3)), f(make()), f(direct)); }
