// [over.over]/1 with the name looked up where it is written: a member function's bare name finds the
// class's static overloads before the namespace's ([basic.lookup.unqual]/8), `S::sf` and `&S::sf` name
// static ones, and a candidate another argument rules out has no say. cpp11 read the global `h` inside
// a member, a silent wrong value, until 2026-10-20.
extern "C" int printf(const char *, ...);
int h(int x) { return x + 1; }
int h(double x) { return (int)(x * 10); }
int one(int x) { return x * 3; }
int ap(int (*g)(int)) { return g(3); }
int apd(int (*g)(double)) { return g(5.0); }
int viaRef(int (*const &g)(int)) { return g(6); }
int pick(int (*f)(int), int *) { return 1; }
int pick(int (*f)(double), int) { return f(1) + 100; }
struct One { static int one(int) { return 50; } int go() { return ap(one); } };
struct X {
    static int h(int) { return 50; }
    static int h(double) { return 60; }
    int go() { return ap(h); }
    static int sgo() { return apd(h); }
    int later();
    int local() { int (*p)(int) = h; return p(0); }
};
int X::later() { return ap(h) + apd(&h); }
struct Y : X { int go() { return ap(h) + 1; } };
namespace n {
int h(int) { return 70; }
int h(double) { return 80; }
struct Z { int go() { return ap(h); } };
}
int main() {
    One o;
    X x;
    Y y;
    n::Z z;
    printf("%d %d %d %d %d\n", o.go(), x.go(), X::sgo(), x.later(), x.local());
    printf("%d %d\n", y.go(), z.go());
    printf("%d %d %d %d\n", ap(X::h), apd(X::h), ap(&X::h), apd(&X::h));
    printf("%d %d %d %d\n", ap((h)), ap((&h)), viaRef(h), pick(h, 1));
    return 0;
}
