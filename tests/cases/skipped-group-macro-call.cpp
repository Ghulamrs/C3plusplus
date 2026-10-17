// [cpp.cond]/6: a skipped group is scanned for directives and expands nothing.
// cpp11 expanded every skipped line on its own, so a function-like macro call
// spanning two lines - the ordinary shape of an assert in a header included a
// second time behind its guard - was "missing its ')'", and __COUNTER__ counted
// in text that never compiles. Comments are still recognised there, so one
// opened in a skipped group and closed after it must keep its meaning.
#include "skipped-group-macro-call.h"
#include "skipped-group-macro-call.h"
extern "C" int printf(const char *, ...);

#define ADD(a, b) ((a) + (b))
#if 0
int never() { return ADD(1,
                         2); }
int counted = __COUNTER__;
#endif

#if 0
/* a comment opened in a skipped group
#endif
   still open here */
#endif

int main() {
    printf("%d %d %d\n", bump(4), ADD(1,
                                       2), __COUNTER__);
    return 0;
}
