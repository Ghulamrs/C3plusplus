// [dcl.constexpr]/2: a constexpr function is implicitly inline, so two translation units may each define
// it and the program has one - the shape of a header's constexpr helper. cpp11 emitted it as a strong
// symbol in each, and the link refused the program; found linking cxx3's own src/ with itself.
extern "C" int printf(const char *, ...);
constexpr int sq(int x) { return x * x; }
namespace n { constexpr bool zero(int r) { return r == 0; } }
typedef int (*Fn)(int);
Fn otherSq();
int otherUse(int);
int main() {
    printf("%d %d %d %d\n", sq(7), (int)n::zero(0), otherUse(3), otherSq()(5));
    return 0;
}
