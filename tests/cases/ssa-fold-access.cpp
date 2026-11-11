// A gep folded into the load or store that reads it (base + index * scale + displacement), and a 32-bit division by a
// constant as one 64-bit multiply: arrays of each width indexed by i, i + 1, i - 1, through a pointer and a global.
#include <stdio.h>

int g[64];
short h[64];
double d[32];
unsigned char c[128];
long long w[16];
struct P { int x, y; };
P ps[8];

int shift(int *a, int n) {
    int moved = 0;
    for (int j = n - 2; j >= 0; --j) {
        if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; ++moved; }
    }
    return moved;
}

void fill(int n) {
    for (int i = 0; i < n; ++i) {
        g[i] = (i * 37) % 64 - 20;
        h[i] = (short)(i / 7 - i % 5);
        c[i * 2] = (unsigned char)('a' + i % 26);
        c[i * 2 + 1] = (unsigned char)(i / -3 + 100);
    }
    for (int i = 0; i < n / 2; ++i) d[i] = i * 0.5 + d[i ? i - 1 : 0];
    for (int i = 1; i < 16; ++i) w[i] = w[i - 1] * 3 + i;
    for (int i = 0; i < 8; ++i) { ps[i].x = i; ps[i].y = ps[i].x * i - g[i + 3]; }
}

unsigned divs(unsigned u, int s) {
    return u / 10u + u % 7u + (unsigned)(s / 9) + (unsigned)(s % 11) + (unsigned)(s / -6) + u / 1000000u;
}

int main() {
    fill(64);
    long long sum = 0;
    for (int i = 0; i < 64; ++i) sum += g[i] * (i + 1) + h[i] - c[i] + c[127 - i];
    for (int i = 0; i < 32; ++i) sum += (long long)(d[i] * 4);
    for (int i = 0; i < 16; ++i) sum ^= w[i];
    for (int i = 0; i < 8; ++i) sum += ps[i].y;
    int moved = 0;
    for (int r = 0; r < 64; ++r) moved += shift(g, 64);
    printf("%lld %d %d %d %d\n", sum, moved, g[0], g[63], h[5]);
    printf("%u %u %u %u\n", divs(123456789u, -98765), divs(4294967295u, 2147483647), divs(7u, -2147483647 - 1), divs(0u, 5));
    return 0;
}
