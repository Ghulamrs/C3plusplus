// [basic.lookup.argdep]/2: every argument's namespaces are searched, not the first two (R110).
// f is found only through its third argument; h's better candidate only through it.
extern "C" { int printf(const char *, ...); }

namespace n { struct X {}; int f(int a, int b, X) { return 1 + 0 * (a + b); } }
namespace k { struct Z {}; int h(int a, int b, Z) { return 2 + 0 * (a + b); } }
int h(double a, double b, k::Z) { return 3 + 0 * (int)(a + b); }

int main(void) {
    printf("%d\n", f(1, 2, n::X()));
    printf("%d\n", h(1, 2, k::Z()));
    return 0;
}
