// offsetof yields an integral constant expression ([support.types]/4 with [expr.const]), so it
// may sit in a static_assert or an array bound. cpp11 defines it as `(size_t)&((T *)0)->m`,
// which its constant folder does not fold: "the condition of a 'static_assert' has to be a
// constant the compiler can work out here". Found writing the anonymous-union case, which
// prints its offsets instead.
#include <stddef.h>
struct S { int k; union { int i; double d; }; };
static_assert(offsetof(S, d) == 8, "the union sits at 8");
char a[offsetof(S, d)];
int main() { return (int)sizeof(a) - 8; }
