// M6a.2 W4: 64-bit and floating values on the C6000 selector - a long long and a double in a register pair (A5:A4 the
// result, each argument its register's pair), a float in one register, the C674x's own instructions and the EABI's
// helpers for the rest; among a variadic call's words a 64-bit one lies 8-aligned, and va_arg reads it there.
#include <stdarg.h>
#include <stdio.h>

typedef long long i64;
typedef unsigned long long u64;

i64 add64(i64 a, i64 b) { return a + b; }
i64 sub64(i64 a, i64 b) { return a - b; }
i64 mul64(i64 a, i64 b) { return a * b; }
i64 div64(i64 a, i64 b) { return a / b; }
i64 rem64(i64 a, i64 b) { return a % b; }
u64 udiv64(u64 a, u64 b) { return a / b; }
u64 urem64(u64 a, u64 b) { return a % b; }
i64 bits64(i64 a, i64 b) { return ((a & b) | (a ^ 0x0f0f0f0f0f0f0f0fLL)) ^ ~b; }
i64 neg64(i64 a) { return -a; }
i64 shl64(i64 a, int n) { return a << n; }
i64 sar64(i64 a, int n) { return a >> n; }
u64 shr64(u64 a, int n) { return a >> n; }
int cmp64(i64 a, i64 b) { return (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b) + 16 * (a == b) + 32 * (a != b); }
int ucmp64(u64 a, u64 b) { return (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b); }
i64 widen(int x) { return x; }
u64 widenu(unsigned x) { return x; }
i64 widenc(signed char c, unsigned short h) { return (i64)c * 1000 + h; }
int narrow(i64 x) { return (int)x; }
short narrowh(i64 x) { return (short)x; }
i64 mixed(int a, i64 b, int c, i64 d) { return a + b * c - d; }
i64 sum64(int n) { i64 s = 0; for (int k = 1; k <= n; k++) s += (i64)k * k * k * 1000; return s; }
i64 pick64(int w, i64 a, i64 b) { i64 r = b; if (w) r = a; return r + 1; }
i64 fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
int sdiv7(int x) { return x / 7 + x % 7 * 100; }

double addd(double a, double b) { return a + b; }
double subd(double a, double b) { return a - b; }
double muld(double a, double b) { return a * b; }
double divd(double a, double b) { return a / b; }
double negd(double a) { return -a; }
int cmpd(double a, double b) { return (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b) + 16 * (a == b) + 32 * (a != b); }
double poly(double x) { return 1.5 + x * (2.25 + x * 0.125); }
double sumd(int n) { double s = 0.0; for (int k = 1; k <= n; k++) s = s + 1.0 / k; return s; }
double fromi(int x) { return x; }
double fromu(unsigned x) { return x; }
double froml(i64 x) { return (double)x; }
int toi(double x) { return (int)x; }
unsigned tou(double x) { return (unsigned)x; }
i64 tol(double x) { return (i64)x; }
double mixd(int a, double b, i64 c, float d) { return a + b * (double)c - d; }

float addf(float a, float b) { return a + b; }
float mulf(float a, float b) { return a * b - 0.5f; }
float divf(float a, float b) { return a / b; }
float negf(float a) { return -a; }
int cmpf(float a, float b) { return (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b) + 16 * (a == b) + 32 * (a != b); }
float fromd(double x) { return (float)x; }
double tod(float x) { return x; }
float fromif(int x) { return (float)x; }
int toif(float x) { return (int)x; }
float sumf(int n) { float s = 0.0f; for (int k = 0; k < n; k++) s = s + 0.25f * k; return s; }

int kind(i64 v) {
    switch (v) {
    case 5: return 1;
    case 0x500000005LL: return 2;
    case -1: return 3;
    case 0x7fffffff00000000LL: return 4;
    }
    return 0;
}
i64 address(const char *p, const char *q) { return (i64)(p - q) + (i64)(p != 0); }
double vsum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double s = 0.0;
    for (int k = 0; k < n; k++) {
        if (k % 3 == 0) s = s + va_arg(ap, int);
        else if (k % 3 == 1) s = s + va_arg(ap, double);
        else s = s + (double)va_arg(ap, i64);
    }
    va_end(ap);
    return s;
}

void show(const char *what, i64 v) { printf("%s %d %u\n", what, (int)(v >> 32), (unsigned)v); }
void showd(const char *what, double v) { printf("%s %d\n", what, (int)(v * 1000.0)); }

int main() {
    const i64 big = 0x123456789abcdefLL, small = -987654321LL;
    show("add", add64(big, small));
    show("sub", sub64(small, big));
    show("mul", mul64(small, 100003));
    show("div", div64(big, small));
    show("rem", rem64(big, small));
    show("udiv", (i64)udiv64(0xfedcba9876543210ULL, 1000003));
    show("urem", (i64)urem64(0xfedcba9876543210ULL, 1000003));
    show("bits", bits64(big, small));
    show("neg", neg64(big));
    show("shl", shl64(big, 5));
    show("shl40", shl64(big, 40));
    show("sar", sar64(small, 3));
    show("sar35", sar64(-big, 35));
    show("shr", (i64)shr64(0xfedcba9876543210ULL, 36));
    printf("cmp %d %d %d %d\n", cmp64(big, small), cmp64(small, big), cmp64(big, big), cmp64(big, big + 1));
    printf("ucmp %d %d %d\n", ucmp64(1, 0xffffffff00000000ULL), ucmp64(0xffffffff00000000ULL, 1), ucmp64(7, 7));
    show("widen", widen(-5));
    show("widenu", (i64)widenu(0xfffffffbu));
    show("widenc", widenc(-3, 65535));
    printf("narrow %d %d\n", narrow(big), narrowh(big));
    show("mixed", mixed(7, big, -3, small));
    show("sum", sum64(300));
    show("pick", pick64(1, big, small));
    show("pick0", pick64(0, big, small));
    show("fact", fact(20));
    printf("sdiv7 %d %d\n", sdiv7(1000), sdiv7(-1000));
    showd("addd", addd(1.25, 2.5));
    showd("subd", subd(1.25, 2.5));
    showd("muld", muld(-1.25, 2.5));
    showd("divd", divd(10.0, 4.0));
    showd("negd", negd(3.75));
    printf("cmpd %d %d %d\n", cmpd(1.5, 2.5), cmpd(2.5, 1.5), cmpd(2.5, 2.5));
    showd("poly", poly(3.0));
    showd("sumd", sumd(50));
    showd("fromi", fromi(-42));
    showd("fromu", fromu(4000000000u) / 1000000.0);
    showd("froml", froml(big) / 1e12);
    printf("to %d %u\n", toi(-7.9), tou(3000000000.5));
    show("tol", tol(-1e15));
    showd("mixd", mixd(3, 0.5, 1000, 2.0f));
    showd("addf", addf(1.5f, 2.25f));
    showd("mulf", mulf(1.5f, 2.0f));
    showd("divf", divf(9.0f, 4.0f));
    showd("negf", negf(0.75f));
    printf("cmpf %d %d %d\n", cmpf(1.5f, 2.5f), cmpf(2.5f, 1.5f), cmpf(2.5f, 2.5f));
    showd("fromd", fromd(0.1));
    showd("tod", tod(0.375f));
    showd("fromif", fromif(-9));
    printf("toif %d\n", toif(-9.75f));
    showd("sumf", sumf(40));
    printf("kind %d %d %d %d %d\n", kind(5), kind(0x500000005LL), kind(-1), kind(0x7fffffff00000000LL), kind(6));
    const char *text = "0123456789";
    show("address", address(text + 7, text));
    showd("vsum", vsum(6, 1, 2.5, (i64)300, -4, 0.125, (i64)-50));
    printf("words %d %.3f %d %.2f\n", 1, 2.5, 3, -0.75);
    printf("pairs %.1f %d %d %.4f %d\n", 1e3, 7, 8, 0.0625, 9);
    return 0;
}
