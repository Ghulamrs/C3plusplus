// [expr.cond]/6: a const lvalue against a prvalue of its class is a const prvalue, which the copy assignment
// takes ([over.match.oper]); the move assignment's `T &&` cannot bind it. Either arm first.
#include <string>
extern "C" int printf(const char *, ...);

namespace {
struct Value { int kind = 0; int k = 0; std::string cc; };
}
static int made = 0;
static Value fresh() { Value v; v.kind = 1; v.k = ++made; v.cc = "fresh"; return v; }

int main() {
    Value regs[4];
    const Value out = fresh();
    for (int d = 0; d < 2; d++) regs[d] = d == 0 ? out : fresh();
    for (int d = 2; d < 4; d++) regs[d] = d == 2 ? fresh() : out;
    for (int d = 0; d < 4; d++) printf("%d %d %s\n", regs[d].kind, regs[d].k, regs[d].cc.c_str());
    printf("%d %s\n", out.k, out.cc.c_str());
    return 0;
}
