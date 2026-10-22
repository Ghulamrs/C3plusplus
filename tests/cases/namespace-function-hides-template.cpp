// [basic.lookup.unqual]/1 for a call: an ordinary function in an enclosing namespace hides a template of
// a namespace no enclosing one is - `copy(r)` in `mir` is `mir::copy`, not `std::copy` (M1, MirWebs.cpp).
#include <algorithm>
extern "C" int printf(const char *, ...);
namespace opt { struct E { int r; }; }
namespace mir {
using namespace opt;
namespace {
E copy(int reg) { E e = { reg * 10 }; return e; }
}
int count(int a, int b) { return a + b; }
int run() { E e = copy(3); return e.r + count(1, 2); }
}
int main() {
    int a[3] = { 1, 2, 3 }, b[3] = { 0, 0, 0 };
    std::copy(a, a + 3, b);
    printf("%d %d\n", mir::run(), b[0] + b[1] + b[2]);
    return 0;
}
