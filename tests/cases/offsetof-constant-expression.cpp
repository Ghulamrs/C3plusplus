// offsetof yields an integral constant expression ([support.types]/4 with [expr.const]), so it
// may sit in a static_assert, an array bound, an enumerator, a case label or a template
// argument. cpp11 defines it as `(size_t)&((T *)0)->m`, which its constant folder did not fold:
// "the condition of a 'static_assert' has to be a constant ..." (found writing anonymous unions).
#include <stddef.h>
extern "C" int printf(const char *, ...);
struct S { int k; union { int i; double d; }; };
struct In { char c; int y; };
struct Outer { char tag; In in; double last; };
static_assert(offsetof(S, d) == 8, "the union sits at 8");
static_assert(offsetof(Outer, in.y) == 8, "a nested member");
char a[offsetof(S, d)];
enum { LastAt = offsetof(Outer, last) };
template <size_t N> struct At { static int get() { return (int)N; } };
int where(size_t off) {
    switch (off) {
    case offsetof(Outer, tag): return 1;
    case offsetof(Outer, in): return 2;
    case offsetof(Outer, last): return 3;
    }
    return 0;
}
int main() {
    printf("%d %d %d %d %d\n", (int)sizeof(a), (int)LastAt, At<offsetof(In, y)>::get(),
           where(offsetof(Outer, in)), where(16));
    return 0;
}
