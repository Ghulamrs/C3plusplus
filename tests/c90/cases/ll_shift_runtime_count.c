// expect: 0
/* A 64-bit shift by a count the compiler cannot see, on both sides of 32.
   The tms6747 backend splices two words for a count under 32 and moves a
   word across for one of 32 or more; the second path read the count as 32
   whatever it was, so every such shift came out as 0 (or the sign) and no
   case here had a runtime count that large. The counts are passed through
   a function so that no front end folds them. */
#include <stdio.h>

static int count(int n) { return n; }

int main(void) {
    long long a = 1, s = -1LL << 60;
    unsigned long long u = 0x8000000000000000ULL;
    int i;
    for (i = 0; i < 64; i += 7) {
        int n = count(i);
        printf("%d: %lld %llu %lld %llu\n", n, a << n, u >> n, s >> n, (u | 5) >> n);
    }
    printf("%lld %lld %llu\n", (a << count(40)) >> count(8),
           (s >> count(33)) << count(31), (u >> count(63)) << count(32));
    return 0;
}
