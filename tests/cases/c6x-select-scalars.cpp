// M6a: the C6000 selection's first subset - words, compares, loops, a switch, calls, division, pointers, chars.
#include <stdio.h>

static int table[8];
static unsigned char bytes[4] = { 200, 1, 2, 3 };

int gcd(int a, int b) {
    while (b != 0) { int t = a % b; a = b; b = t; }
    return a;
}

unsigned mix(unsigned x, unsigned y) { return (x * 2654435761u) ^ (y >> 3) ^ (x / (y | 1)); }

int classify(int v) {
    switch (v) {
    case 0: return 10;
    case 1: case 2: return 20;
    case 7: return 70;
    default: return v < 0 ? -1 : 99;
    }
}

int fill(int *p, int n, int step) {
    int sum = 0;
    for (int i = 0; i < n; i++) { p[i] = i * step - 3; sum += p[i]; }
    return sum;
}

signed char narrow(int v) { return static_cast<signed char>(v + 1); }

int seven(int a, int b, int c, int d, int e, int f, int g) { return a - b + c - d + e - f + g * 2; }

int byteSum() {
    int s = 0;
    for (int i = 0; i < 4; i++) s += bytes[i];
    return s;
}

int main() {
    printf("%d %d %d\n", gcd(84, 36), gcd(17, 5), gcd(0, 9));
    printf("%u\n", mix(12345u, 678u));
    printf("%d %d %d %d %d\n", classify(0), classify(2), classify(7), classify(-4), classify(50));
    printf("%d %d %d\n", fill(table, 8, 5), table[3], table[7]);
    printf("%d %d\n", narrow(127), narrow(-5));
    printf("%d %d\n", seven(1, 2, 3, 4, 5, 6, 7), byteSum());
    printf("%d %d\n", -7 / 2, -7 % 2);
    return 0;
}
