// [over.over]: an overloaded function's name used as a value names the one function whose type
// the target asks for - an initialiser's, an assignment's, a cast's, a return's. cpp11 refused
// any `&f` or `&S::f` naming more than one function ("which one this is cannot be told from the
// use alone"); found 2026-10-18 with ref-qualified overloads, older than them.
extern "C" int printf(const char *, ...);
struct S {
    int v;
    int f() & { return v; }
    int f() && { return v * 10; }
    int g(int x) { return v + x; }
    int g(double) { return -1; }
};
int h(int x) { return x * 2; }
int h(double) { return 7; }
int h(int a, int b) { return a + b; }
typedef int (*OneInt)(int);
OneInt pickH() { return &h; }
int main() {
    int (S::*lv)() & = &S::f;
    int (S::*rv)() && = &S::f;
    int (S::*gi)(int) = &S::g;
    int (S::*gd)(double);
    gd = &S::g;
    S s;
    s.v = 4;
    printf("%d %d %d %d\n", (s.*lv)(), (static_cast<S &&>(s).*rv)(), (s.*gi)(3), (s.*gd)(1.0));
    int (*one)(int) = &h;
    int (*two)(int, int) = h;
    int (*dbl)(double);
    dbl = h;
    int (&ref)(int) = h;
    OneInt cast = (OneInt)&h;
    OneInt named = static_cast<OneInt>(h);
    printf("%d %d %d %d %d %d %d\n", one(5), two(2, 3), dbl(1.5), ref(6), cast(1), named(2), pickH()(9));
    return 0;
}
