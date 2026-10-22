// [expr.cast]/4: `(const V &)q` and `(int &)k` name an lvalue of the referent - an operand of an
// overloaded operator, a member call's object, a reference's initialiser. cpp11 left the cast's type
// a reference, so `((const V &)q) * 3.0` found no operator and `int &rk = (int &)k` no object.
extern "C" int printf(const char *, ...);
struct V { double x; V operator*(double s) const { V r; r.x = x * s; return r; } double get() const { return x; } };
struct Q : V { int tag; };
double first(const V &v) { return v.x; }
int main() {
    Q q; q.x = 2; q.tag = 1;
    V a = ((const V &)q) * 3.0;
    double b = ((const V &)q).get();
    double c = first((const V &)q);
    V &m = (V &)q; m.x = 5;
    int k = 7; int &rk = (int &)k; rk = 9;
    printf("%g %g %g %g %d\n", a.x, b, c, q.x, k);
}
