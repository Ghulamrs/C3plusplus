// **`B &&` binds a derived rvalue** - [dcl.init.ref]/5 - and beats `const B &` for one,
// [over.ics.rank]/3.2.3; an lvalue still takes the const reference. Found by
// `W::operator=(std::move(e))` in the compiler's own C6xModel.h (M1, 2026-10-23).
#include <utility>
extern "C" { int printf(const char *, ...); }

struct B { int v; };
struct D : B { int k; };

static int take(const B &b) { return 1 + 0 * b.v; }
static int take(B &&b) { return 2 + 0 * b.v; }

int main(void) {
    D d;
    d.v = 0;
    d.k = 0;
    const int a = take(d);
    const int b = take(std::move(d));
    const int c = take(D());
    printf("%d %d %d\n", a, b, c);
    return 0;
}
