// [dcl.init.ref]/5: a `T &&` binds the temporary a conversion makes - a converting constructor or a standard
// conversion - and [over.ics.rank]/3.3 with /3.2.3 prefers it to `const T &` for that same temporary; a class
// reference-related to the argument takes no temporary, so a derived lvalue still reaches only `const B &`.
extern "C" int printf(const char *, ...);
struct S { int v; S(int k) : v(k) {} };
struct B { int b; };
struct D : B { int d; };
static int f(const S &) { return 1; }
static int f(S &&) { return 2; }
static int g(const double &) { return 3; }
static int g(double &&) { return 4; }
static int h(double &&d) { return (int)d; }
static int k(const B &) { return 5; }
static int k(B &&) { return 6; }
int main() {
    D d;
    d.b = 1;
    printf("%d %d %d %d %d\n", f(5), g(5), h(7), k(d), k(D()));
    return 0;
}
