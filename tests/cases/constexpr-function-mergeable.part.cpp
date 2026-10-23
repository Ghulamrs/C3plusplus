// The second translation unit of constexpr-function-mergeable.cpp, defining the same constexpr functions.
constexpr int sq(int x) { return x * x; }
namespace n { constexpr bool zero(int r) { return r == 0; } }
typedef int (*Fn)(int);
Fn otherSq() { return &sq; }
int otherUse(int a) { return sq(a) + n::zero(a); }
