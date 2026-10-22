// A namespace-scope initialiser that makes a temporary - `int g = f(S(3));` - is read before the
// init function's frame is entered, and its temporaries' slots were never reserved there: the
// program wrote past the frame and died before main. They are counted into it now.
extern "C" int printf(const char *, ...);

struct S { int a; S(int x) : a(x) {} };
int f(S s) { return s.a; }
int g = f(S(3)) + f(S(4));
double d = f(S(5)) * 0.5;

int main() {
    printf("%d %g\n", g, d);
    return 0;
}
