// [expr.comma] with [class.copy]/32: `return a, std::move(p);` returns the comma's right operand, an xvalue, which
// the move constructor takes; cxx3 refuses it as a class "with no usable copy or move constructor" (rv-mend, 2026-11-13).
#include <memory>
#include <utility>
extern "C" int printf(const char *, ...);
static int marked = 0;
std::unique_ptr<int> made(int v) {
    std::unique_ptr<int> p(new int(v));
    return marked++, std::move(p);
}
int main() {
    std::unique_ptr<int> q = made(7);
    printf("%d %d\n", *q, marked);
    return 0;
}
