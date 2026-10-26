// M3b.2: the reserved placement new lowered to SSA - no allocation, the given address constructed into: a scalar
// stored, a plain class copied or value-initialised, an array zeroed - into a buffer that outlives them all.
#include <new>
extern "C" int printf(const char *, ...);

struct P { int a; int b; };

int main() {
    alignas(8) unsigned char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 0xff;
    int *n = new (buf) int(42);
    P src = { 7, 8 };
    P *q = new (buf + 8) P(src);
    P *z = new (buf + 16) P();
    int *arr = new (buf + 24) int[4]();
    double *d = new (buf + 40) double(2.5);
    printf("%d %d %d %d %d %d %g %d\n", *n, q->a, q->b, z->a + z->b, arr[0] + arr[3], (int)((unsigned char *)q - buf), *d, (int)buf[60]);
    return 0;
}
