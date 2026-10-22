// [expr.const]/4: a constexpr pointer takes an address constant - null, a string literal, or an
// object of static storage with an offset - and a second one may be defined from the first. The
// compiler's own Name.h writes `constexpr const char *kName = "cpp11";` (M1 L6, 2026-10-23).
extern "C" int printf(const char *, ...);
int table[4] = {10, 20, 30, 40};
constexpr const char *kName = "cpp11";
static constexpr const char *kOther = kName;
constexpr int *third = &table[2];
constexpr int *nothing = nullptr;
namespace n { constexpr const char *inner = "ns"; }
struct S { static constexpr const char *tag = "s"; };
constexpr const char *S::tag;
int main() {
    constexpr const char *local = "loc";
    constexpr const int *shifted = third + 1;
    printf("%s %s %s %s %s %c %d %d %d\n", kName, kOther, n::inner, S::tag, local, kName[2], *third,
           *shifted, nothing == nullptr);
    return 0;
}
